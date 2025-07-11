/*
 * Copyright (C) 2014-2022 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WKProcessPoolInternal.h"

#import "AutomationClient.h"
#import "CacheModel.h"
#import "Connection.h"
#import "DownloadManager.h"
#import "GPUProcessProxy.h"
#import "LegacyDownloadClient.h"
#import "Logging.h"
#import "NetworkProcessProxy.h"
#import "ProcessTerminationReason.h"
#import "SandboxUtilities.h"
#import "UIGamepadProvider.h"
#import "WKAPICast.h"
#import "WKDownloadInternal.h"
#import "WKObject.h"
#import "WKWebViewInternal.h"
#import "WKWebsiteDataStoreInternal.h"
#import "WebBackForwardCache.h"
#import "WebNotificationManagerProxy.h"
#import "WebPageProxy.h"
#import "WebProcessCache.h"
#import "WebProcessMessages.h"
#import "WebProcessPool.h"
#import "_WKAutomationDelegate.h"
#import "_WKAutomationSessionInternal.h"
#import "_WKDownloadDelegate.h"
#import "_WKDownloadInternal.h"
#import "_WKProcessPoolConfigurationInternal.h"
#import <WebCore/CertificateInfo.h>
#import <WebCore/PluginData.h>
#import <WebCore/RegistrableDomain.h>
#import <WebCore/WebCoreObjCExtras.h>
#import <pal/spi/cf/CFNetworkSPI.h>
#import <wtf/BlockPtr.h>
#import <wtf/RetainPtr.h>
#import <wtf/WeakObjCPtr.h>
#import <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#import <wtf/cocoa/VectorCocoa.h>

#if PLATFORM(IOS_FAMILY)
#import <WebCore/WebCoreThreadSystemInterface.h>
#import "WKGeolocationProviderIOS.h"
#endif

@interface _WKProcessInfo()
- (instancetype)initWithTaskInfo:(const WebKit::AuxiliaryProcessProxy::TaskInfo&)info;
@end

@interface _WKWebContentProcessInfo()
- (instancetype)initWithTaskInfo:(const WebKit::AuxiliaryProcessProxy::TaskInfo&)info process:(const WebKit::WebProcessProxy&)process;
@end

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
static RetainPtr<WKProcessPool>& sharedProcessPool()
{
    static NeverDestroyed<RetainPtr<WKProcessPool>> sharedProcessPool;
    return sharedProcessPool;
}
ALLOW_DEPRECATED_DECLARATIONS_END

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
@implementation WKProcessPool {
    WeakObjCPtr<id <_WKAutomationDelegate>> _automationDelegate;
    WeakObjCPtr<id <_WKDownloadDelegate>> _downloadDelegate;

    RetainPtr<_WKAutomationSession> _automationSession;
#if PLATFORM(IOS_FAMILY)
    RetainPtr<WKGeolocationProviderIOS> _geolocationProvider;
    RetainPtr<id <_WKGeolocationCoreLocationProvider>> _coreLocationProvider;
#endif // PLATFORM(IOS_FAMILY)
}
ALLOW_DEPRECATED_IMPLEMENTATIONS_END

WK_OBJECT_DISABLE_DISABLE_KVC_IVAR_ACCESS;

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
- (instancetype)_initWithConfiguration:(_WKProcessPoolConfiguration *)configuration
{
    if (!(self = [super init]))
        return nil;

    API::Object::constructInWrapper<WebKit::WebProcessPool>(self, *configuration->_processPoolConfiguration);

    return self;
}

- (instancetype)init
{
    return [self _initWithConfiguration:adoptNS([[_WKProcessPoolConfiguration alloc] init]).get()];
}
ALLOW_DEPRECATED_DECLARATIONS_END

- (void)dealloc
{
    if (WebCoreObjCScheduleDeallocateOnMainRunLoop(WKProcessPool.class, self))
        return;

    _processPool->~WebProcessPool();

    [super dealloc];
}

+ (BOOL)supportsSecureCoding
{
    return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
    if (self == sharedProcessPool()) {
        [coder encodeBool:YES forKey:@"isSharedProcessPool"];
        return;
    }
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    if (!(self = [self init]))
        return nil;

    if ([coder decodeBoolForKey:@"isSharedProcessPool"]) {
        [self release];

        return [[WKProcessPool _sharedProcessPool] retain];
    }

    return self;
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<%@: %p; configuration = %@>", NSStringFromClass(self.class), self, wrapper(_processPool->configuration())];
}

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
- (_WKProcessPoolConfiguration *)_configuration
{
    return wrapper(_processPool->configuration().copy()).autorelease();
}
ALLOW_DEPRECATED_DECLARATIONS_END

- (API::Object&)_apiObject
{
    return *_processPool;
}

#if PLATFORM(IOS_FAMILY)
- (WKGeolocationProviderIOS *)_geolocationProvider
{
    if (!_geolocationProvider)
        _geolocationProvider = adoptNS([[WKGeolocationProviderIOS alloc] initWithProcessPool:*_processPool]);
    return _geolocationProvider.get();
}
#endif // PLATFORM(IOS_FAMILY)

@end

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
@implementation WKProcessPool (WKPrivate)
ALLOW_DEPRECATED_IMPLEMENTATIONS_END

+ (WKProcessPool *)_sharedProcessPool
{
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedProcessPool() = adoptNS([[WKProcessPool alloc] init]);
    });

    return sharedProcessPool().get();
}

+ (NSArray<WKProcessPool *> *)_allProcessPoolsForTesting
{
    return createNSArray(WebKit::WebProcessPool::allProcessPools(), [] (auto&& pool) {
        return wrapper(pool.get());
    }).autorelease();
}

+ (NSURL *)_websiteDataURLForContainerWithURL:(NSURL *)containerURL
{
    return [WKProcessPool _websiteDataURLForContainerWithURL:containerURL bundleIdentifierIfNotInContainer:nil];
}

+ (NSURL *)_websiteDataURLForContainerWithURL:(NSURL *)containerURL bundleIdentifierIfNotInContainer:(NSString *)bundleIdentifier
{
    NSURL *url = [containerURL URLByAppendingPathComponent:@"Library" isDirectory:YES];
    url = [url URLByAppendingPathComponent:@"WebKit" isDirectory:YES];

    if (!WebKit::processHasContainer() && bundleIdentifier)
        url = [url URLByAppendingPathComponent:bundleIdentifier isDirectory:YES];

    return [url URLByAppendingPathComponent:@"WebsiteData" isDirectory:YES];
}

- (void)_setAllowsSpecificHTTPSCertificate:(NSArray *)certificateChain forHost:(NSString *)host
{
}

- (void)_registerURLSchemeAsCanDisplayOnlyIfCanRequest:(NSString *)scheme
{
    _processPool->registerURLSchemeAsCanDisplayOnlyIfCanRequest(scheme);
}

- (void)_registerURLSchemeAsSecure:(NSString *)scheme
{
    _processPool->registerURLSchemeAsSecure(scheme);
}

- (void)_registerURLSchemeAsBypassingContentSecurityPolicy:(NSString *)scheme
{
    _processPool->registerURLSchemeAsBypassingContentSecurityPolicy(scheme);
}

- (void)_setDomainRelaxationForbiddenForURLScheme:(NSString *)scheme
{
    _processPool->setDomainRelaxationForbiddenForURLScheme(scheme);
}

- (void)_setCanHandleHTTPSServerTrustEvaluation:(BOOL)value
{
}

- (id)_objectForBundleParameter:(NSString *)parameter
{
    return [_processPool->bundleParameters() objectForKey:parameter];
}

- (void)_setObject:(id <NSCopying, NSSecureCoding>)object forBundleParameter:(NSString *)parameter
{
    auto copy = adoptNS([(NSObject *)object copy]);
    auto keyedArchiver = adoptNS([[NSKeyedArchiver alloc] initRequiringSecureCoding:YES]);

    @try {
        [keyedArchiver encodeObject:copy.get() forKey:@"parameter"];
        [keyedArchiver finishEncoding];
    } @catch (NSException *exception) {
        LOG_ERROR("Failed to encode bundle parameter: %@", exception);
    }

    if (copy)
        [_processPool->ensureBundleParameters() setObject:copy.get() forKey:parameter];
    else
        [_processPool->ensureBundleParameters() removeObjectForKey:parameter];

    auto data = keyedArchiver.get().encodedData;
    _processPool->sendToAllProcesses(Messages::WebProcess::SetInjectedBundleParameter(parameter, span(data)));
}

- (void)_setObjectsForBundleParametersWithDictionary:(NSDictionary *)dictionary
{
    auto copy = adoptNS([[NSDictionary alloc] initWithDictionary:dictionary copyItems:YES]);
    auto keyedArchiver = adoptNS([[NSKeyedArchiver alloc] initRequiringSecureCoding:YES]);

    @try {
        [keyedArchiver encodeObject:copy.get() forKey:@"parameters"];
        [keyedArchiver finishEncoding];
    } @catch (NSException *exception) {
        LOG_ERROR("Failed to encode bundle parameters: %@", exception);
    }

    [_processPool->ensureBundleParameters() setValuesForKeysWithDictionary:copy.get()];

    auto data = keyedArchiver.get().encodedData;
    _processPool->sendToAllProcesses(Messages::WebProcess::SetInjectedBundleParameters(span(data)));
}

#if !TARGET_OS_IPHONE

- (void)_resetPluginLoadClientPolicies:(NSDictionary *)policies
{
}

-(NSDictionary *)_pluginLoadClientPolicies
{
    return @{ };
}

#endif

- (id <_WKDownloadDelegate>)_downloadDelegate
{
    return _downloadDelegate.getAutoreleased();
}

- (void)_setDownloadDelegate:(id <_WKDownloadDelegate>)downloadDelegate
{
    _downloadDelegate = downloadDelegate;
    _processPool->setLegacyDownloadClient(adoptRef(*new WebKit::LegacyDownloadClient(downloadDelegate)));
}

- (id <_WKAutomationDelegate>)_automationDelegate
{
    return _automationDelegate.getAutoreleased();
}

- (void)_setAutomationDelegate:(id <_WKAutomationDelegate>)automationDelegate
{
    _automationDelegate = automationDelegate;
    _processPool->setAutomationClient(makeUnique<WebKit::AutomationClient>(self, automationDelegate));
}

- (void)_warmInitialProcess
{
    _processPool->prewarmProcess();
}

- (void)_automationCapabilitiesDidChange
{
    _processPool->updateAutomationCapabilities();
}

- (void)_setAutomationSession:(_WKAutomationSession *)automationSession
{
    _automationSession = automationSession;
    _processPool->setAutomationSession(automationSession ? automationSession->_session.get() : nullptr);
}

- (NSURL *)_javaScriptConfigurationDirectory
{
    return [NSURL fileURLWithPath:_processPool->javaScriptConfigurationDirectory().createNSString().get() isDirectory:YES];
}

- (void)_setJavaScriptConfigurationDirectory:(NSURL *)directory
{
    if (directory && ![directory isFileURL])
        [NSException raise:NSInvalidArgumentException format:@"%@ is not a file URL", directory];
    _processPool->setJavaScriptConfigurationDirectory(directory.path);
}

- (void)_addSupportedPlugin:(NSString *) domain named:(NSString *) name withMimeTypes: (NSSet<NSString *> *) nsMimeTypes withExtensions: (NSSet<NSString *> *) nsExtensions
{
    HashSet<String> mimeTypes;
    for (NSString *mimeType in nsMimeTypes)
        mimeTypes.add(mimeType);
    HashSet<String> extensions;
    for (NSString *extension in nsExtensions)
        extensions.add(extension);

    _processPool->addSupportedPlugin(domain, name, WTFMove(mimeTypes), WTFMove(extensions));
}

- (void)_clearSupportedPlugins
{
    _processPool->clearSupportedPlugins();
}

- (void)_terminateServiceWorkers
{
    _processPool->terminateServiceWorkers();
}

- (void)_setUseSeparateServiceWorkerProcess:(BOOL)useSeparateServiceWorkerProcess
{
    WebKit::WebProcessPool::setUseSeparateServiceWorkerProcess(useSeparateServiceWorkerProcess);
}

- (pid_t)_prewarmedProcessIdentifier
{
    return _processPool->prewarmedProcessID();
}


- (void)_clearWebProcessCache
{
    _processPool->webProcessCache().clear();
}

- (size_t)_webProcessCount
{
    return _processPool->processes().size();
}

- (pid_t)_gpuProcessIdentifier
{
#if ENABLE(GPU_PROCESS)
    RefPtr gpuProcess = _processPool->gpuProcess();
    return gpuProcess ? gpuProcess->processID() : 0;
#else
    return 0;
#endif
}

- (BOOL)_hasAudibleMediaActivity
{
    return _processPool->hasAudibleMediaActivity() ? YES : NO;
}

- (BOOL)_requestWebProcessTermination:(pid_t)pid
{
    for (Ref process : _processPool->processes()) {
        if (process->processID() == pid)
            process->requestTermination(WebKit::ProcessTerminationReason::RequestedByClient);
        return YES;
    }
    return NO;
}

- (BOOL)_isWebProcessSuspended:(pid_t)pid
{
    for (Ref process : _processPool->processes()) {
        if (process->processID() == pid)
            return process->throttler().isSuspended();
    }
    return NO;
}

- (void)_makeNextWebProcessLaunchFailForTesting
{
    _processPool->setShouldMakeNextWebProcessLaunchFailForTesting(true);
}

- (BOOL)_hasPrewarmedWebProcess
{
    for (Ref process : _processPool->processes()) {
        if (process->isPrewarmed())
            return YES;
    }
    return NO;
}

- (size_t)_webProcessCountIgnoringPrewarmed
{
    return [self _webProcessCount] - ([self _hasPrewarmedWebProcess] ? 1 : 0);
}

- (size_t)_webProcessCountIgnoringPrewarmedAndCached
{
    size_t count = 0;
    for (Ref process : _processPool->processes()) {
        if (!process->isInProcessCache() && !process->isPrewarmed())
            ++count;
    }
    return count;
}

- (size_t)_webPageContentProcessCount
{
    auto result = _processPool->processes().size();
    if (_processPool->useSeparateServiceWorkerProcess())
        result -= _processPool->serviceWorkerProxiesCount();
    return result;
}

- (void)_preconnectToServer:(NSURL *)serverURL
{
}

- (size_t)_pluginProcessCount
{
    return 0;
}

- (NSUInteger)_maximumSuspendedPageCount
{
    return _processPool->backForwardCache().capacity();
}

- (NSUInteger)_processCacheCapacity
{
    return _processPool->webProcessCache().capacity();
}

- (NSUInteger)_processCacheSize
{
    return _processPool->webProcessCache().size();
}

- (size_t)_serviceWorkerProcessCount
{
    return _processPool->serviceWorkerProxiesCount();
}

- (void)_isJITDisabledInAllRemoteWorkerProcesses:(void(^)(BOOL))completionHandler
{
    _processPool->isJITDisabledInAllRemoteWorkerProcesses([completionHandler = makeBlockPtr(completionHandler)] (bool result) {
        completionHandler(result);
    });
}

+ (void)_forceGameControllerFramework
{
#if ENABLE(GAMEPAD)
    WebKit::UIGamepadProvider::setUsesGameControllerFramework();
#endif
}

+ (void)_setLinkedOnOrBeforeEverythingForTesting
{
    disableAllSDKAlignedBehaviors();
}

+ (void)_setLinkedOnOrAfterEverythingForTesting
{
    [self _setLinkedOnOrAfterEverything];
}

+ (void)_crashOnMessageCheckFailureForTesting
{
    IPC::Connection::setShouldCrashOnMessageCheckFailure(true);
}

+ (void)_setLinkedOnOrAfterEverything
{
    enableAllSDKAlignedBehaviors();
}

+ (void)_setCaptivePortalModeEnabledGloballyForTesting:(BOOL)isEnabled
{
    WebKit::setLockdownModeEnabledGloballyForTesting(!!isEnabled);
}

+ (BOOL)_lockdownModeEnabledGloballyForTesting
{
    return WebKit::lockdownModeEnabledBySystem();
}

+ (void)_clearCaptivePortalModeEnabledGloballyForTesting
{
    WebKit::setLockdownModeEnabledGloballyForTesting(std::nullopt);
}

+ (void)_setEnableMetalDebugDeviceInNewGPUProcessesForTesting:(BOOL)enable
{
    WebKit::GPUProcessProxy::setEnableMetalDebugDeviceInNewGPUProcessesForTesting(enable);
}

+ (void)_setEnableMetalShaderValidationInNewGPUProcessesForTesting:(BOOL)enable
{
    WebKit::GPUProcessProxy::setEnableMetalShaderValidationInNewGPUProcessesForTesting(enable);
}

+ (BOOL)_isMetalDebugDeviceEnabledInGPUProcessForTesting
{
    if (RefPtr gpuProcess = WebKit::GPUProcessProxy::singletonIfCreated())
        return gpuProcess->isMetalDebugDeviceEnabledForTesting();
    return WebKit::GPUProcessProxy::isMetalDebugDeviceEnabledInNewGPUProcessesForTesting();
}

+ (BOOL)_isMetalShaderValidationEnabledInGPUProcessForTesting
{
    if (RefPtr gpuProcess = WebKit::GPUProcessProxy::singletonIfCreated())
        return gpuProcess->isMetalShaderValidationEnabledForTesting();
    return WebKit::GPUProcessProxy::isMetalShaderValidationEnabledInNewGPUProcessesForTesting();
}

- (BOOL)_isCookieStoragePartitioningEnabled
{
    return _processPool->cookieStoragePartitioningEnabled();
}

- (void)_setCookieStoragePartitioningEnabled:(BOOL)enabled
{
    _processPool->setCookieStoragePartitioningEnabled(enabled);
}

#if PLATFORM(IOS_FAMILY)
- (id <_WKGeolocationCoreLocationProvider>)_coreLocationProvider
{
    return _coreLocationProvider.get();
}

- (void)_setCoreLocationProvider:(id<_WKGeolocationCoreLocationProvider>)coreLocationProvider
{
    if (_geolocationProvider)
        [NSException raise:NSGenericException format:@"Changing the location provider is not supported after a web view in the process pool has begun servicing geolocation requests."];

    _coreLocationProvider = coreLocationProvider;
}
#endif // PLATFORM(IOS_FAMILY)

- (void)_getActivePagesOriginsInWebProcessForTesting:(pid_t)pid completionHandler:(void(^)(NSArray<NSString *> *))completionHandler
{
    _processPool->activePagesOriginsInWebProcessForTesting(pid, [completionHandler = makeBlockPtr(completionHandler)] (Vector<String>&& activePagesOrigins) {
        completionHandler(createNSArray(activePagesOrigins).get());
    });
}

- (void)_clearPermanentCredentialsForProtectionSpace:(NSURLProtectionSpace *)protectionSpace
{
    _processPool->clearPermanentCredentialsForProtectionSpace(WebCore::ProtectionSpace(protectionSpace));
}

- (void)_seedResourceLoadStatisticsForTestingWithFirstParty:(NSURL *)firstPartyURL thirdParty:(NSURL *)thirdPartyURL shouldScheduleNotification:(BOOL)shouldScheduleNotification completionHandler:(void(^)(void))completionHandler
{
    _processPool->seedResourceLoadStatisticsForTesting(WebCore::RegistrableDomain { firstPartyURL }, WebCore::RegistrableDomain { thirdPartyURL }, shouldScheduleNotification, [completionHandler = makeBlockPtr(completionHandler)] () {
        completionHandler();
    });
}

+ (void)_setWebProcessCountLimit:(unsigned)limit
{
    WebKit::WebProcessProxy::setProcessCountLimit(limit);
}

- (void)_garbageCollectJavaScriptObjectsForTesting
{
    _processPool->garbageCollectJavaScriptObjects();
}

- (size_t)_numberOfConnectedGamepadsForTesting
{
    return _processPool->numberOfConnectedGamepadsForTesting(WebKit::WebProcessPool::GamepadType::All);
}

- (size_t)_numberOfConnectedHIDGamepadsForTesting
{
    return _processPool->numberOfConnectedGamepadsForTesting(WebKit::WebProcessPool::GamepadType::HID);
}

- (size_t)_numberOfConnectedGameControllerFrameworkGamepadsForTesting
{
    return _processPool->numberOfConnectedGamepadsForTesting(WebKit::WebProcessPool::GamepadType::GameControllerFramework);
}

- (void)_setUsesOnlyHIDGamepadProviderForTesting:(BOOL)usesHIDProvider
{
    _processPool->setUsesOnlyHIDGamepadProviderForTesting(usesHIDProvider);
}

- (void)_terminateAllWebContentProcesses
{
    _processPool->terminateAllWebContentProcesses(WebKit::ProcessTerminationReason::RequestedByClient);
}

- (WKNotificationManagerRef)_notificationManagerForTesting
{
    return WebKit::toAPI(_processPool->supplement<WebKit::WebNotificationManagerProxy>());
}

+ (_WKProcessInfo *)_gpuProcessInfo
{
    RetainPtr<_WKProcessInfo> result;

    if (RefPtr gpuProcess = WebKit::GPUProcessProxy::singletonIfCreated()) {
        if (auto taskInfo = gpuProcess->taskInfo())
            result = adoptNS([[_WKProcessInfo alloc] initWithTaskInfo:*taskInfo]);
    }

    return result.autorelease();
}

+ (NSArray<_WKProcessInfo *> *)_networkingProcessInfo
{
    RetainPtr result = adoptNS([NSMutableArray new]);

    for (auto& networkProcess : WebKit::NetworkProcessProxy::allNetworkProcesses()) {
        if (auto taskInfo = networkProcess->taskInfo())
            [result addObject:adoptNS([[_WKProcessInfo alloc] initWithTaskInfo:*taskInfo]).get()];
    }

    return result.autorelease();
}

+ (NSArray<_WKProcessInfo *> *)_webContentProcessInfo
{
    RetainPtr result = adoptNS([NSMutableArray new]);

    for (auto& webProcessPool : WebKit::WebProcessPool::allProcessPools()) {
        for (auto& webProcess : webProcessPool->processes()) {
            if (auto taskInfo = webProcess->taskInfo())
                [result addObject:adoptNS([[_WKWebContentProcessInfo alloc] initWithTaskInfo:*taskInfo process:webProcess.get()]).get()];
        }
    }

    return result.autorelease();
}

#if PLATFORM(MAC)
- (void)_registerAdditionalFonts:(NSArray<NSString *> *)fontNames
{
    _processPool->registerAdditionalFonts(fontNames);
}
#endif

@end


@implementation _WKProcessInfo {
    pid_t _pid;
    _WKProcessState _state;
    NSTimeInterval _totalUserCPUTime;
    NSTimeInterval _totalSystemCPUTime;
    size_t _physicalFootprint;
}

@synthesize pid = _pid;
@synthesize state = _state;
@synthesize totalUserCPUTime = _totalUserCPUTime;
@synthesize totalSystemCPUTime = _totalSystemCPUTime;
@synthesize physicalFootprint = _physicalFootprint;

static _WKProcessState processStateFromThrottleState(WebKit::ProcessThrottleState state)
{
    switch (state) {
    case WebKit::ProcessThrottleState::Foreground:
        return _WKProcessStateForeground;
    case WebKit::ProcessThrottleState::Background:
        return _WKProcessStateBackground;
    case WebKit::ProcessThrottleState::Suspended:
        return _WKProcessStateSuspended;
    default:
        ASSERT_NOT_REACHED();
        return _WKProcessStateForeground;
    }
}

- (instancetype)initWithTaskInfo:(const WebKit::AuxiliaryProcessProxy::TaskInfo&)info
{
    if (!(self = [super init]))
        return nil;

    _pid = info.pid;
    _state = processStateFromThrottleState(info.state);
    _totalUserCPUTime = info.totalUserCPUTime.seconds();
    _totalSystemCPUTime = info.totalSystemCPUTime.seconds();
    _physicalFootprint = info.physicalFootprint;

    return self;
}

@end


@implementation _WKWebContentProcessInfo {
    _WKWebContentProcessState _webContentState;
    RetainPtr<NSMutableArray<WKWebView *>> _webViews;
    BOOL _runningServiceWorkers;
    BOOL _runningSharedWorkers;
    NSTimeInterval _totalForegroundTime;
    NSTimeInterval _totalBackgroundTime;
    NSTimeInterval _totalSuspendedTime;
}

@synthesize webContentState = _webContentState;
@synthesize runningServiceWorkers = _runningServiceWorkers;
@synthesize runningSharedWorkers = _runningSharedWorkers;
@synthesize totalForegroundTime = _totalForegroundTime;
@synthesize totalBackgroundTime = _totalBackgroundTime;
@synthesize totalSuspendedTime = _totalSuspendedTime;

- (instancetype)initWithTaskInfo:(const WebKit::AuxiliaryProcessProxy::TaskInfo&)info process:(const WebKit::WebProcessProxy&)process
{
    if (!(self = [super initWithTaskInfo:info]))
        return nil;

    _webContentState = _WKWebContentProcessStateActive;
    if (process.isPrewarmed())
        _webContentState = _WKWebContentProcessStatePrewarmed;
    else if (process.isInProcessCache())
        _webContentState = _WKWebContentProcessStateCached;

    if (_webContentState == _WKWebContentProcessStateActive) {
        for (auto& page : process.pages()) {
            if (auto webView = page->cocoaView()) {
                if (!_webViews)
                    _webViews = adoptNS([[NSMutableArray alloc] init]);
                [_webViews addObject:webView.get()];
            }
        }
    }

    _runningServiceWorkers = process.isRunningServiceWorkers();
    _runningSharedWorkers = process.isRunningSharedWorkers();

    _totalForegroundTime = process.totalForegroundTime().seconds();
    _totalBackgroundTime = process.totalBackgroundTime().seconds();
    _totalSuspendedTime = process.totalSuspendedTime().seconds();

    return self;
}

- (NSArray<WKWebView *> *)webViews
{
    return _webViews.get();
}

@end
