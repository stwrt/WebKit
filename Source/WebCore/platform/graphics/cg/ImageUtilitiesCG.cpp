/*
 * Copyright (C) 2020-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ImageUtilities.h"

#include "FloatRect.h"
#include "GraphicsContext.h"
#include "ImageBuffer.h"
#include "ImageDecoderCG.h"
#include "Logging.h"
#include "MIMETypeRegistry.h"
#include "SVGImage.h"
#include "SVGImageForContainer.h"
#include "UTIRegistry.h"
#include "UTIUtilities.h"
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>
#include <WebCore/ShareableBitmap.h>
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/cf/VectorCF.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

WorkQueue& sharedImageTranscodingQueueSingleton()
{
    static NeverDestroyed<Ref<WorkQueue>> queue(WorkQueue::create("com.apple.WebKit.ImageTranscoding"_s));
    return queue.get();
}

static String transcodeImage(const String& path, const String& destinationUTI, const String& destinationExtension)
{
    auto sourceURL = adoptCF(CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path.createCFString().get(), kCFURLPOSIXPathStyle, false));
    auto source = adoptCF(CGImageSourceCreateWithURL(sourceURL.get(), nullptr));
    if (!source)
        return nullString();

    auto sourceUTI = String(CGImageSourceGetType(source.get()));
    if (!sourceUTI || sourceUTI == destinationUTI)
        return nullString();

    // It is important to add the appropriate file extension to the temporary file path.
    // The File object depends solely on the extension to know the MIME type of the file.
    auto suffix = makeString('.', destinationExtension);
    auto [destinationPath, destinationFileHandle] = FileSystem::openTemporaryFile("tempImage"_s, suffix);
    if (!destinationFileHandle) {
        RELEASE_LOG_ERROR(Images, "transcodeImage: Destination image could not be created: %s %s\n", path.utf8().data(), destinationUTI.utf8().data());
        return nullString();
    }

    CGDataConsumerCallbacks callbacks = {
        [](void* info, const void* buffer, size_t count) -> size_t {
            auto& handle = *static_cast<FileSystem::FileHandle*>(info);
            return handle.write(unsafeMakeSpan(static_cast<const uint8_t*>(buffer), count)).value_or(0);
        },
        nullptr
    };

    auto consumer = adoptCF(CGDataConsumerCreate(&destinationFileHandle, &callbacks));
    auto destination = adoptCF(CGImageDestinationCreateWithDataConsumer(consumer.get(), destinationUTI.createCFString().get(), 1, nullptr));

    CGImageDestinationAddImageFromSource(destination.get(), source.get(), 0, nullptr);

    if (!CGImageDestinationFinalize(destination.get())) {
        RELEASE_LOG_ERROR(Images, "transcodeImage: Image transcoding fails: %s %s\n", path.utf8().data(), destinationUTI.utf8().data());
        destinationFileHandle = { };
        FileSystem::deleteFile(destinationPath);
        return nullString();
    }

    return destinationPath;
}

Vector<String> findImagesForTranscoding(const Vector<String>& paths, const Vector<String>& allowedMIMETypes)
{
    bool needsTranscoding = false;
    auto transcodingPaths = paths.map([&](auto& path) {
        // Append a path of the image which needs transcoding. Otherwise append a null string.
        if (!allowedMIMETypes.contains(WebCore::MIMETypeRegistry::mimeTypeForPath(path))) {
            needsTranscoding = true;
            return path;
        }
        return nullString();
    });

    // If none of the files needs image transcoding, return an empty Vector.
    return needsTranscoding ? transcodingPaths : Vector<String>();
}

Vector<String> transcodeImages(const Vector<String>& paths, const String& destinationUTI, const String& destinationExtension)
{
    ASSERT(!destinationUTI.isNull());
    ASSERT(!destinationExtension.isNull());
    
    return paths.map([&](auto& path) {
        // Append the transcoded path if the image needs transcoding. Otherwise append a null string.
        return path.isNull() ? nullString() : transcodeImage(path, destinationUTI, destinationExtension);
    });
}

String descriptionString(ImageDecodingError error)
{
    switch (error) {
    case ImageDecodingError::Internal:
        return "Internal error"_s;
    case ImageDecodingError::BadData:
        return "Bad data"_s;
    case ImageDecodingError::UnsupportedType:
        return "Unsupported image type"_s;
    }

    return "Unkown error"_s;
}

Expected<std::pair<String, Vector<IntSize>>, ImageDecodingError> utiAndAvailableSizesFromImageData(std::span<const uint8_t> data)
{
    Ref buffer = FragmentedSharedBuffer::create(data);
    Ref imageDecoder = ImageDecoderCG::create(buffer.get(), AlphaOption::Premultiplied, GammaAndColorProfileOption::Applied);
    imageDecoder->setData(buffer.get(), true);
    if (imageDecoder->encodedDataStatus() == EncodedDataStatus::Error)
        return makeUnexpected(ImageDecodingError::BadData);

    auto uti = imageDecoder->uti();
    if (!isSupportedImageType(uti))
        return makeUnexpected(ImageDecodingError::UnsupportedType);

    size_t frameCount = imageDecoder->frameCount();
    // Do not support animated image.
    if (imageDecoder->repetitionCount() != RepetitionCountNone && frameCount > 1)
        return makeUnexpected(ImageDecodingError::UnsupportedType);

    Vector<IntSize> sizes;
    sizes.reserveInitialCapacity(frameCount);
    for (size_t index = 0; index < frameCount; ++index)
        sizes.append(imageDecoder->frameSizeAtIndex(index));

    return std::make_pair(WTFMove(uti), WTFMove(sizes));
}

static RefPtr<NativeImage> tryCreateNativeImageFromBitmapImageData(std::span<const uint8_t> data, std::optional<FloatSize> preferredSize)
{
    Ref buffer = FragmentedSharedBuffer::create(data);
    Ref imageDecoder = ImageDecoderCG::create(buffer.get(), AlphaOption::Premultiplied, GammaAndColorProfileOption::Applied);
    imageDecoder->setData(buffer.get(), true);
    if (imageDecoder->encodedDataStatus() == EncodedDataStatus::Error)
        return nullptr;

    auto sourceUTI = imageDecoder->uti();
    if (!isSupportedImageType(sourceUTI))
        return nullptr;

    auto preferredIndex = [&]() -> size_t {
        if (!preferredSize)
            return imageDecoder->primaryFrameIndex();
        size_t frameCount = imageDecoder->frameCount();
        for (size_t index = 0; index < frameCount; ++index) {
            if (imageDecoder->frameSizeAtIndex(index) == *preferredSize)
                return index;
        }
        return imageDecoder->primaryFrameIndex();
    };
    RetainPtr image = imageDecoder->createFrameImageAtIndex(preferredIndex());
    if (!image)
        return nullptr;

    return NativeImage::create(WTFMove(image));
}

static void tryCreateNativeImageFromData(std::span<const uint8_t> data, std::optional<FloatSize> preferredSize, CompletionHandler<void(RefPtr<NativeImage>&&)>&& completionHandler)
{
    if (RefPtr nativeImage = tryCreateNativeImageFromBitmapImageData(data, preferredSize)) {
        completionHandler(WTFMove(nativeImage));
        return;
    }
    SVGImage::tryCreateFromData(data, [completionHandler = WTFMove(completionHandler)](auto svgImage) mutable {
        if (!svgImage) {
            completionHandler(nullptr);
            return;
        }
        completionHandler(svgImage->nativeImage(svgImage->size()));
    });
}

static RefPtr<SharedBuffer> expandNativeImageToData(NativeImage& image, ASCIILiteral uti, std::span<const unsigned> lengths)
{
    RetainPtr colorSpace = adoptCF(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    RetainPtr destinationData = adoptCF(CFDataCreateMutable(0, 0));
    RetainPtr cfUTI = uti.createCFString();
    RetainPtr destination = adoptCF(CGImageDestinationCreateWithData(destinationData.get(), cfUTI.get(), lengths.size(), nullptr));
    for (auto length : lengths) {
        RetainPtr context = adoptCF(CGBitmapContextCreate(nullptr, length, length, 8, length * 4, colorSpace.get(), kCGImageAlphaPremultipliedLast));
        if (!context)
            return nullptr;

        // Quality is more important than rendering speed for the current use case.
        CGContextSetInterpolationQuality(context.get(), kCGInterpolationHigh);
        CGContextDrawImage(context.get(), CGRectMake(0, 0, length, length), image.platformImage().get());
        RetainPtr newImage = adoptCF(CGBitmapContextCreateImage(context.get()));
        if (!newImage)
            return nullptr;

        CGImageDestinationAddImage(destination.get(), newImage.get(), nullptr);
    }

    if (!CGImageDestinationFinalize(destination.get()))
        return nullptr;

    return SharedBuffer::create(destinationData.get());
}

static RefPtr<SharedBuffer> expandSVGImageToData(SVGImage& svgImage, ASCIILiteral uti, std::span<const unsigned> lengths)
{
    RetainPtr colorSpace = adoptCF(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    RetainPtr destinationData = adoptCF(CFDataCreateMutable(0, 0));
    RetainPtr cfUTI = uti.createCFString();
    RetainPtr destination = adoptCF(CGImageDestinationCreateWithData(destinationData.get(), cfUTI.get(), lengths.size(), nullptr));
    for (auto length : lengths) {
        auto size = FloatSize { float(length), float(length) };
        RefPtr buffer = ImageBuffer::create(size, RenderingMode::Unaccelerated, RenderingPurpose::Unspecified, 1, DestinationColorSpace::SRGB(), ImageBufferPixelFormat::BGRA8);
        if (!buffer)
            return nullptr;

        Ref svgImageContainer = SVGImageForContainer::create(&svgImage, size, 1, { });
        buffer->context().drawImage(svgImageContainer.get(), FloatPoint::zero());

        RefPtr newImage = ImageBuffer::sinkIntoNativeImage(WTFMove(buffer));
        if (!newImage)
            return nullptr;

        CGImageDestinationAddImage(destination.get(), newImage->platformImage().get(), nullptr);
    }

    if (!CGImageDestinationFinalize(destination.get()))
        return nullptr;

    return SharedBuffer::create(destinationData.get());
}

void createIconDataFromImageData(std::span<const uint8_t> data, std::span<const unsigned> lengths, CompletionHandler<void(RefPtr<SharedBuffer>&&)>&& completionHandler)
{
    // Supported ICO image sizes by ImageIO.
    constexpr std::array<unsigned, 5> availableLengths { { 16, 32, 48, 128, 256 } };
    constexpr auto icoUTI = "com.microsoft.ico"_s;
    auto targetLengths = lengths.empty() ? std::span { availableLengths } : lengths;

    if (RefPtr nativeImage = tryCreateNativeImageFromBitmapImageData(data, std::nullopt)) {
        completionHandler(expandNativeImageToData(*nativeImage, icoUTI, targetLengths));
        return;
    }

    SVGImage::tryCreateFromData(data, [icoUTI, targetLengthsVector = Vector<unsigned> { targetLengths }, completionHandler = WTFMove(completionHandler)](auto svgImage) mutable {
        if (!svgImage) {
            completionHandler(nullptr);
            return;
        }
        completionHandler(expandSVGImageToData(*svgImage, icoUTI, targetLengthsVector.span()));
    });
}

// FIXME: This does not implement preferredSize for SVG at the moment as there are no callers that pass preferredSize.
void decodeImageWithSize(std::span<const uint8_t> data, std::optional<FloatSize> preferredSize, CompletionHandler<void(RefPtr<ShareableBitmap>&&)>&& completionHandler)
{
    tryCreateNativeImageFromData(data, preferredSize, [completionHandler = WTFMove(completionHandler)](auto nativeImage) mutable {
        if (!nativeImage) {
            completionHandler(nullptr);
            return;
        }

        auto sourceColorSpace = nativeImage->colorSpace();
        auto destinationColorSpace = sourceColorSpace.supportsOutput() ? sourceColorSpace : DestinationColorSpace::SRGB();
        RefPtr bitmap = ShareableBitmap::create({ nativeImage->size(), destinationColorSpace });
        if (!bitmap) {
            completionHandler(nullptr);
            return;
        }

        auto context = bitmap->createGraphicsContext();
        if (!context) {
            completionHandler(nullptr);
            return;
        }

        FloatRect rect { { }, nativeImage->size() };
        context->drawNativeImage(*nativeImage, rect, rect, { CompositeOperator::Copy });
        completionHandler(WTFMove(bitmap));
    });
}

} // namespace WebCore
