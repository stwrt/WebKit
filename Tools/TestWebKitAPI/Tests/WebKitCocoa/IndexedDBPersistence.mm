/*
 * Copyright (C) 2016-2022 Apple Inc. All rights reserved.
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

#import "DeprecatedGlobalValues.h"
#import "PlatformUtilities.h"
#import "Test.h"
#import "TestURLSchemeHandler.h"
#import "TestWKWebView.h"
#import <WebCore/SQLiteFileSystem.h>
#import <WebKit/WKPreferencesPrivate.h>
#import <WebKit/WKPreferencesRefPrivate.h>
#import <WebKit/WKProcessPoolPrivate.h>
#import <WebKit/WKUserContentControllerPrivate.h>
#import <WebKit/WKWebViewConfigurationPrivate.h>
#import <WebKit/WKWebViewPrivate.h>
#import <WebKit/WKWebsiteDataStorePrivate.h>
#import <WebKit/WebKit.h>
#import <WebKit/_WKProcessPoolConfiguration.h>
#import <WebKit/_WKUserStyleSheet.h>
#import <WebKit/_WKWebsiteDataStoreConfiguration.h>
#import <wtf/FileHandle.h>
#import <wtf/FileSystem.h>
#import <wtf/RetainPtr.h>

static RetainPtr<NSString> selectedFilePath;

@interface IndexedDBMessageHandler : NSObject <WKScriptMessageHandler>
@end

@implementation IndexedDBMessageHandler

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message
{
    receivedScriptMessage = true;
    scriptMessages.append(message);
}

@end

@interface IndexedDBOpenPanelUIDelegate : NSObject <WKUIDelegate>
@end

@implementation IndexedDBOpenPanelUIDelegate

- (void)webView:(WKWebView *)webView runOpenPanelWithParameters:(WKOpenPanelParameters *)parameters initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(NSArray<NSURL *> *))completionHandler
{
    EXPECT_FALSE(parameters.allowsMultipleSelection);
    EXPECT_FALSE(parameters.allowsDirectories);
    constexpr auto TestFileData = "This is test file"_s;
    auto [path, handle] = FileSystem::openTemporaryFile("IndexedDBPersistence"_s);
    handle.write(TestFileData.span8());
    handle = { };
    selectedFilePath = path.createNSString();
    completionHandler(@[ [NSURL fileURLWithPath:selectedFilePath.get()] ]);
}

@end

TEST(IndexedDB, IndexedDBPersistence)
{
    RetainPtr<IndexedDBMessageHandler> handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    RetainPtr<WKWebViewConfiguration> configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];

    RetainPtr<WKWebView> webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    NSURLRequest *request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IndexedDBPersistence-1" withExtension:@"html"]];
    [webView loadRequest:request];

    RetainPtr<NSString> string1 = (NSString *)[getNextMessage() body];
    RetainPtr<NSString> string2 = (NSString *)[getNextMessage() body];

    // Ditch this web view (ditching its web process)
    webView = nil;

    // Terminate the network process
    [configuration.get().websiteDataStore _terminateNetworkProcess];

    // Make a new web view to finish the test
    webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IndexedDBPersistence-2" withExtension:@"html"]];
    [webView loadRequest:request];
    RetainPtr<NSString> string3 = (NSString *)[getNextMessage() body];

    EXPECT_WK_STREQ(@"UpgradeNeeded", string1.get());
    EXPECT_WK_STREQ(@"Success", string2.get());
    EXPECT_WK_STREQ(@"2 TestObjectStore", string3.get());
}

TEST(IndexedDB, IndexedDBPersistencePrivate)
{
    RetainPtr<IndexedDBMessageHandler> handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    RetainPtr<WKWebViewConfiguration> configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];

    auto ephemeralStore = [WKWebsiteDataStore nonPersistentDataStore];
    configuration.get().websiteDataStore = ephemeralStore;

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    NSURLRequest *request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IndexedDBPersistence-1" withExtension:@"html"]];
    [webView loadRequest:request];

    RetainPtr<NSString> string1 = (NSString *)[getNextMessage() body];
    RetainPtr<NSString> string2 = (NSString *)[getNextMessage() body];

    auto webViewPid1 = [webView _webProcessIdentifier];
    // Ditch this web view (ditching its web process)
    webView = nil;

    // Make a new web view to finish the test
    webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IndexedDBPersistence-2" withExtension:@"html"]];
    [webView loadRequest:request];
    RetainPtr<NSString> string3 = (NSString *)[getNextMessage() body];

    auto webViewPid2 = [webView _webProcessIdentifier];
    EXPECT_NE(webViewPid1, webViewPid2);

    EXPECT_WK_STREQ(@"UpgradeNeeded", string1.get());
    EXPECT_WK_STREQ(@"Success", string2.get());
    EXPECT_WK_STREQ(@"2 TestObjectStore", string3.get());
}

TEST(IndexedDB, IndexedDBDataRemoval)
{
    auto websiteDataTypes = adoptNS([[NSSet alloc] initWithArray:@[WKWebsiteDataTypeIndexedDBDatabases]]);

    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] removeDataOfTypes:websiteDataTypes.get() modifiedSince:[NSDate distantPast] completionHandler:^() {
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] fetchDataRecordsOfTypes:websiteDataTypes.get() completionHandler:^(NSArray<WKWebsiteDataRecord *> *dataRecords) {
        readyToContinue = true;
        ASSERT_EQ(0u, dataRecords.count);
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    receivedScriptMessage = false;
    auto handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    NSURLRequest *request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IndexedDBPersistence-1" withExtension:@"html"]];
    [webView loadRequest:request];
    TestWebKitAPI::Util::run(&receivedScriptMessage);

    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] fetchDataRecordsOfTypes:websiteDataTypes.get() completionHandler:^(NSArray<WKWebsiteDataRecord *> *dataRecords) {
        ASSERT_EQ(1u, dataRecords.count);
        [[WKWebsiteDataStore defaultDataStore] removeDataOfTypes:websiteDataTypes.get() forDataRecords:dataRecords completionHandler:^() {
            readyToContinue = true;
        }];
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] fetchDataRecordsOfTypes:websiteDataTypes.get() completionHandler:^(NSArray<WKWebsiteDataRecord *> *dataRecords) {
        readyToContinue = true;
        ASSERT_EQ(0u, dataRecords.count);
    }];
    TestWebKitAPI::Util::run(&readyToContinue);
}

static NSString *mainFrameStringPersistence = @"<script> \
    function postResult(event) { \
        window.webkit.messageHandlers.testHandler.postMessage(event.data); \
    } \
    addEventListener('message', postResult, false); \
    </script> \
    <iframe src='iframe://'>";

static const char* iframeBytesPersistence = R"TESTRESOURCE(
<script>
function postResult(result) {
    if (window.parent != window.top) {
        parent.postMessage(result, '*');
    } else {
        window.webkit.messageHandlers.testHandler.postMessage(result);
    }
}

try {
    var request = window.indexedDB.open('IndexedDBThirdPartyFrameHasAccess');
    var db = null;
    request.onupgradeneeded = function(event) {
        db = event.target.result;
        var objectStore = db.createObjectStore('TestObjectStore');
        var putRequest = objectStore.put('TestValue', 'TestKey');
        putRequest.onsuccess = function(event) {
            postResult('database is created - put item success');
        }
        putRequest.onerror = function(event) {
            postResult('database is created - put item error: ' + event.target.error.name + ' - ' + event.target.error.message);
        }
    }
    request.onsuccess = function(event) {
        if (db)
            return;
        db = event.target.result;
        var objectStore = db.transaction(['TestObjectStore']).objectStore('TestObjectStore');
        var getRequest = objectStore.get('TestKey');
        getRequest.onsuccess = function(event) {
            postResult('database exists - get item success: ' + event.target.result);
        }
        getRequest.onerror = function(event) {
            postResult('database exists - get item error: ' + event.target.error.name + ' - ' + event.target.error.message);
        }
    }
    request.onerror = function(event) {
        if (!db) {
            postResult('database error: ' + event.target.error.name + ' - ' + event.target.error.message);
        }
    }
} catch(err) {
    postResult('database error: ' + err.name + ' - ' + err.message);
}
</script>
)TESTRESOURCE";

static void loadThirdPartyPageInWebView(WKWebView *webView, NSString *expectedResult)
{
    [webView loadHTMLString:mainFrameStringPersistence baseURL:[NSURL URLWithString:@"http://webkit.org"]];
    EXPECT_WK_STREQ(expectedResult, (NSString *)[getNextMessage() body]);
}

TEST(IndexedDB, IndexedDBThirdPartyFrameHasAccess)
{
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    auto handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    auto schemeHandler = adoptNS([[TestURLSchemeHandler alloc] init]);
    [schemeHandler setStartURLSchemeTaskHandler:^(WKWebView *, id<WKURLSchemeTask> task) {
        auto response = adoptNS([[NSURLResponse alloc] initWithURL:task.request.URL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]);
        [task didReceiveResponse:response.get()];
        [task didReceiveData:[NSData dataWithBytes:iframeBytesPersistence length:strlen(iframeBytesPersistence)]];
        [task didFinish];
    }];
    [configuration setURLSchemeHandler:schemeHandler.get() forURLScheme:@"iframe"];

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(webView.get(), @"database is created - put item success");

    auto secondWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(secondWebView.get(), @"database exists - get item success: TestValue");

    webView = nil;
    secondWebView = nil;
    [configuration.get().websiteDataStore _terminateNetworkProcess];

    // Third-party IDB storage is stored in the memory of network process.
    auto thirdWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(thirdWebView.get(), @"database is created - put item success");
}

TEST(IndexedDB, IndexedDBThirdPartyDataRemoval)
{
    auto websiteDataTypes = adoptNS([[NSSet alloc] initWithArray:@[WKWebsiteDataTypeIndexedDBDatabases]]);
    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] removeDataOfTypes:websiteDataTypes.get() modifiedSince:[NSDate distantPast] completionHandler:^() {
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    [WKWebsiteDataStore _allowWebsiteDataRecordsForAllOrigins];

    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    auto handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    auto schemeHandler = adoptNS([[TestURLSchemeHandler alloc] init]);
    [schemeHandler setStartURLSchemeTaskHandler:^(WKWebView *, id<WKURLSchemeTask> task) {
        auto response = adoptNS([[NSURLResponse alloc] initWithURL:task.request.URL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]);
        [task didReceiveResponse:response.get()];
        [task didReceiveData:[NSData dataWithBytes:iframeBytesPersistence length:strlen(iframeBytesPersistence)]];
        [task didFinish];
    }];
    [configuration setURLSchemeHandler:schemeHandler.get() forURLScheme:@"iframe"];

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(webView.get(), @"database is created - put item success");

    auto secondWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [secondWebView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"iframe://"]]];
    EXPECT_WK_STREQ( @"database is created - put item success", (NSString *)[getNextMessage() body]);

    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] fetchDataRecordsOfTypes:websiteDataTypes.get() completionHandler:^(NSArray<WKWebsiteDataRecord *> *records) {
        EXPECT_EQ(2u, records.count);
        NSSortDescriptor *recordDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"displayName" ascending:YES];
        NSArray *sortDescriptors = @[recordDescriptor];
        NSArray *sortedRecords = [records sortedArrayUsingDescriptors:sortDescriptors];
        EXPECT_WK_STREQ("iframe", [[sortedRecords objectAtIndex:0] displayName]);
        EXPECT_WK_STREQ("webkit.org", [[sortedRecords objectAtIndex:1] displayName]);
        [[WKWebsiteDataStore defaultDataStore] removeDataOfTypes:websiteDataTypes.get() forDataRecords:records completionHandler:^() {
            readyToContinue = true;
        }];
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    auto thirdWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(thirdWebView.get(), @"database is created - put item success");
}

TEST(IndexedDB, IndexedDBThirdPartyStorageLayout)
{
    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    String databaseHash = WebCore::SQLiteFileSystem::computeHashForFileName("IndexedDBThirdPartyFrameHasAccess"_s);
    RetainPtr webkitURL = [NSURL URLWithString:@"http://webkit.org"];
    RetainPtr iframeURL = [NSURL URLWithString:@"iframe://"];
    __block RetainPtr<NSString> directoryString = nil;
    done = false;
    [configuration.get().websiteDataStore _originDirectoryForTesting:iframeURL.get() topOrigin:webkitURL.get() type:WKWebsiteDataTypeIndexedDBDatabases completionHandler:^(NSString *result) {
        directoryString = result;
        done = true;
    }];
    TestWebKitAPI::Util::run(&done);
    RetainPtr webkitIframeRootDirectory = [NSURL fileURLWithPath:directoryString.get() isDirectory:YES];
    RetainPtr webkitIframeDatabaseDirectory = [webkitIframeRootDirectory URLByAppendingPathComponent:databaseHash.createNSString().get()];
    RetainPtr webkitIframeDatabaseFile = [webkitIframeDatabaseDirectory URLByAppendingPathComponent:@"IndexedDB.sqlite3"];

    done = false;
    [configuration.get().websiteDataStore _originDirectoryForTesting:webkitURL.get() topOrigin:iframeURL.get() type:WKWebsiteDataTypeIndexedDBDatabases completionHandler:^(NSString *result) {
        directoryString = result;
        done = true;
    }];
    TestWebKitAPI::Util::run(&done);
    RetainPtr iframeWebKitRootDirectory = [NSURL fileURLWithPath:directoryString.get() isDirectory:YES];

    RetainPtr fileManager = [NSFileManager defaultManager];
    [fileManager removeItemAtURL:webkitIframeRootDirectory.get() error:nil];
    [fileManager removeItemAtURL:iframeWebKitRootDirectory.get() error:nil];

    RetainPtr handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    RetainPtr schemeHandler = adoptNS([[TestURLSchemeHandler alloc] init]);
    [schemeHandler setStartURLSchemeTaskHandler:^(WKWebView *, id<WKURLSchemeTask> task) {
        RetainPtr response = adoptNS([[NSURLResponse alloc] initWithURL:task.request.URL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]);
        [task didReceiveResponse:response.get()];
        [task didReceiveData:[NSData dataWithBytes:iframeBytesPersistence length:strlen(iframeBytesPersistence)]];
        [task didFinish];
    }];
    [configuration setURLSchemeHandler:schemeHandler.get() forURLScheme:@"iframe"];
    // Allowing third-party frame to store data on disk.
    [[configuration preferences] _setStorageBlockingPolicy:_WKStorageBlockingPolicyAllowAll];

    RetainPtr webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(webView.get(), @"database is created - put item success");
    EXPECT_TRUE([fileManager fileExistsAtPath:webkitIframeRootDirectory.get().path]);
    EXPECT_TRUE([fileManager fileExistsAtPath:webkitIframeDatabaseFile.get().path]);
    EXPECT_FALSE([fileManager fileExistsAtPath:iframeWebKitRootDirectory.get().path]);
}

TEST(IndexedDB, MigrateThirdPartyDataToGeneralStorageDirectory)
{
    NSURL *resourceSalt = [NSBundle.test_resourcesBundle URLForResource:@"general-storage-directory" withExtension:@"salt"];
    NSURL *resourceDatabase = [NSBundle.test_resourcesBundle URLForResource:@"indexeddb-persistence-third-party" withExtension:@"sqlite3"];
    NSURL *generalStorageDirectory = [NSURL fileURLWithPath:[@"~/Library/WebKit/com.apple.WebKit.TestWebKitAPI/CustomWebsiteData/Default" stringByExpandingTildeInPath] isDirectory:YES];
    NSURL *webkitOriginDirectory = [generalStorageDirectory URLByAppendingPathComponent:@"EAO66s8JvCWNn4D3YQut5pXfiGF_UXNZGvMGN6aKILg/EAO66s8JvCWNn4D3YQut5pXfiGF_UXNZGvMGN6aKILg"];
    NSURL *webkitOriginFile = [webkitOriginDirectory URLByAppendingPathComponent:@"origin"];
    NSURL *wrongWebkitIframeDatabaseDirectory = [webkitOriginDirectory URLByAppendingPathComponent:@"IndexedDB/iframe__0"];
    NSURL *webkitIframeOriginDirectory = [generalStorageDirectory URLByAppendingPathComponent:@"EAO66s8JvCWNn4D3YQut5pXfiGF_UXNZGvMGN6aKILg/vudvbMlKXj1m6RibnVvc8PdAdcXZsNE6ON_Al7yqOsg"];
    NSURL *webkitIframeOriginFile = [webkitIframeOriginDirectory URLByAppendingPathComponent:@"origin"];
    RetainPtr hashedDatabaseName = WebCore::SQLiteFileSystem::computeHashForFileName("IndexedDBThirdPartyFrameHasAccess"_s).createNSString();
    NSURL *webkitIframeDatabaseFile= [NSURL fileURLWithPath:[NSString pathWithComponents:@[webkitIframeOriginDirectory.path, @"IndexedDB", hashedDatabaseName.get(), @"IndexedDB.sqlite3"]]];
    NSURL *idbDirectory = [NSURL fileURLWithPath:[@"~/Library/WebKit/com.apple.WebKit.TestWebKitAPI/CustomWebsiteData/IndexedDB" stringByExpandingTildeInPath] isDirectory:YES];
    NSURL *oldWebkitIframeDatabaseDirectory = [NSURL fileURLWithPath:[NSString pathWithComponents:@[idbDirectory.path, @"v1/http_webkit.org_0/iframe__0", hashedDatabaseName.get()]]];
    NSURL *oldWebkitIframeDatabaseFile = [oldWebkitIframeDatabaseDirectory URLByAppendingPathComponent:@"IndexedDB.sqlite3"];

    NSFileManager *fileManager = [NSFileManager defaultManager];
    [fileManager removeItemAtURL:generalStorageDirectory error:nil];
    [fileManager createDirectoryAtURL:generalStorageDirectory withIntermediateDirectories:YES attributes:nil error:nil];
    [fileManager copyItemAtURL:resourceSalt toURL:[generalStorageDirectory URLByAppendingPathComponent:@"salt"] error:nil];
    // Create third-party database file on disk.
    [fileManager removeItemAtURL:idbDirectory error:nil];
    [fileManager createDirectoryAtURL:oldWebkitIframeDatabaseDirectory withIntermediateDirectories:YES attributes:nil error:nil];
    [fileManager copyItemAtURL:resourceDatabase toURL:oldWebkitIframeDatabaseFile error:nil];

    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    auto handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    auto schemeHandler = adoptNS([[TestURLSchemeHandler alloc] init]);
    [schemeHandler setStartURLSchemeTaskHandler:^(WKWebView *, id<WKURLSchemeTask> task) {
        auto response = adoptNS([[NSURLResponse alloc] initWithURL:task.request.URL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]);
        [task didReceiveResponse:response.get()];
        [task didReceiveData:[NSData dataWithBytes:iframeBytesPersistence length:strlen(iframeBytesPersistence)]];
        [task didFinish];
    }];
    [configuration setURLSchemeHandler:schemeHandler.get() forURLScheme:@"iframe"];
    [[configuration preferences] _setStorageBlockingPolicy:_WKStorageBlockingPolicyAllowAll];
    auto websiteDataStoreConfiguration = adoptNS([[_WKWebsiteDataStoreConfiguration alloc] init]);
    websiteDataStoreConfiguration.get()._indexedDBDatabaseDirectory = idbDirectory;
    websiteDataStoreConfiguration.get().generalStorageDirectory = generalStorageDirectory;
    websiteDataStoreConfiguration.get().unifiedOriginStorageLevel = _WKUnifiedOriginStorageLevelBasic;
    auto websiteDataStore = adoptNS([[WKWebsiteDataStore alloc] _initWithConfiguration:websiteDataStoreConfiguration.get()]);
    [configuration setWebsiteDataStore:websiteDataStore.get()];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    // Ensure opening first-party database does not migrate third-party data.
    NSString *firstPartyHTMLString = @"<script> \
        var request = window.indexedDB.open('TestDatabase'); \
        request.onsuccess = function() { \
            window.webkit.messageHandlers.testHandler.postMessage('database is created'); \
        }; \
        request.onerror = function() { \
            window.webkit.messageHandlers.testHandler.postMessage('database error'); \
        }; \
        </script>";
    [webView loadHTMLString:firstPartyHTMLString baseURL:[NSURL URLWithString:@"http://webkit.org"]];
    EXPECT_WK_STREQ(@"database is created", (NSString *)[getNextMessage() body]);
    EXPECT_TRUE([fileManager fileExistsAtPath:webkitOriginFile.path]);
    EXPECT_FALSE([fileManager fileExistsAtPath:wrongWebkitIframeDatabaseDirectory.path]);

    // Ensure opening third-party database migrates third-party data, and data can be correctly read.
    loadThirdPartyPageInWebView(webView.get(), @"database exists - get item success: TestValue");
    EXPECT_TRUE([fileManager fileExistsAtPath:webkitIframeOriginFile.path]);
    EXPECT_TRUE([fileManager fileExistsAtPath:webkitIframeDatabaseFile.path]);
}

static const char* workerBytes = R"TESTRESOURCE(
try {
    var request = indexedDB.open('IndexedDBThirdPartyWorkerHasAccess');
    var db = null;
    request.onupgradeneeded = function(event) {
        db = event.target.result;
        self.postMessage('database is created');
    }
    request.onsuccess = function(event) {
        if (db)
            return;
        self.postMessage('database exists');
    }
    request.onerror = function(event) {
        if (!db) {
            self.postMessage('database error: ' + event.target.error.name + ' - ' + event.target.error.message);
        }
    }
} catch(err) {
    self.postMessage('database error: ' + err.name + ' - ' + err.message);
}
)TESTRESOURCE";

static const char* workerFrameBytes = R"TESTRESOURCE(
<script>
    var worker = new Worker('worker.js');
    worker.onmessage = function(event) {
        parent.postMessage(event.data, '*');
    };
</script>
)TESTRESOURCE";

TEST(IndexedDB, IndexedDBThirdPartyWorkerHasAccess)
{
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    auto handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    auto schemeHandler = adoptNS([[TestURLSchemeHandler alloc] init]);
    [schemeHandler setStartURLSchemeTaskHandler:^(WKWebView *, id<WKURLSchemeTask> task) {
        RetainPtr<NSURLResponse> response;
        RetainPtr<NSData> data;
        NSURL *requestURL = task.request.URL;
        if ([requestURL.absoluteString isEqualToString:@"iframe://"]) {
            response = adoptNS([[NSURLResponse alloc] initWithURL:requestURL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]);
            data = [NSData dataWithBytes:workerFrameBytes length:strlen(workerFrameBytes)];
        } else {
            EXPECT_WK_STREQ("iframe:///worker.js", requestURL.absoluteString);
            response = adoptNS([[NSURLResponse alloc] initWithURL:requestURL MIMEType:@"text/javascript" expectedContentLength:0 textEncodingName:nil]);
            data = [NSData dataWithBytes:workerBytes length:strlen(workerBytes)];
        }
        [task didReceiveResponse:response.get()];
        [task didReceiveData:data.get()];
        [task didFinish];
    }];
    [configuration setURLSchemeHandler:schemeHandler.get() forURLScheme:@"iframe"];

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(webView.get(), @"database is created");

    auto secondWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(webView.get(), @"database exists");

    webView = nil;
    secondWebView = nil;
    [configuration.get().websiteDataStore _terminateNetworkProcess];

    auto thirdWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    loadThirdPartyPageInWebView(thirdWebView.get(), @"database is created");
}

static NSString *getDatabasesString = @"<script> \
    function sendMessage(message) { \
        window.webkit.messageHandlers.testHandler.postMessage(message); \
    } \
    function postResult(event) { \
        sendMessage(event.data); \
    } \
    function postDatabases(databases) { \
        sendMessage('databases: ' + JSON.stringify(databases)); \
    } \
    function loadFrame() { \
        var frame = document.createElement('iframe'); \
        frame.src = 'webkit://'; \
        document.body.appendChild(frame); \
    } \
    addEventListener('message', postResult, false); \
    var request = indexedDB.open('IndexedDBGetDatabases'); \
    request.onsuccess = function(event) { \
        indexedDB.databases().then(postDatabases); \
    } \
    </script>";

static const char* getDatabasesBytes = R"TESTRESOURCE(
<script>
function postResult(result) {
    if (window.self !== window.top)
        parent.postMessage(result, '*');
    else
        window.webkit.messageHandlers.testHandler.postMessage(result);
}
function postDatabases(databases) {
    var prefix = 'main frame databases: ';
    if (window.self !== window.top)
        prefix = 'child frame databases: ';
    postResult(prefix + JSON.stringify(databases));
}
indexedDB.databases().then(postDatabases);
</script>
)TESTRESOURCE";

TEST(IndexedDB, IndexedDBGetDatabases)
{
    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] removeDataOfTypes:[NSSet setWithObjects:WKWebsiteDataTypeIndexedDBDatabases, nil] modifiedSince:[NSDate distantPast] completionHandler:^() {
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    RetainPtr handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    RetainPtr schemeHandler = adoptNS([[TestURLSchemeHandler alloc] init]);
    [schemeHandler setStartURLSchemeTaskHandler:^(WKWebView *, id<WKURLSchemeTask> task) {
        RetainPtr response = adoptNS([[NSURLResponse alloc] initWithURL:task.request.URL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]);
        [task didReceiveResponse:response.get()];
        [task didReceiveData:[NSData dataWithBytes:getDatabasesBytes length:strlen(getDatabasesBytes)]];
        [task didFinish];
    }];
    [configuration setURLSchemeHandler:schemeHandler.get() forURLScheme:@"webkit"];

    RetainPtr webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [webView loadHTMLString:getDatabasesString baseURL:[NSURL URLWithString:@"http://apple.com"]];
    EXPECT_WK_STREQ(@"databases: [{\"name\":\"IndexedDBGetDatabases\",\"version\":1}]", [getNextMessage() body]);

    [webView evaluateJavaScript:@"loadFrame()" completionHandler:nil];
    EXPECT_WK_STREQ(@"child frame databases: []", [getNextMessage() body]);

    RetainPtr secondWebView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [secondWebView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"webkit://"]]];
    EXPECT_WK_STREQ(@"main frame databases: []", [getNextMessage() body]);

    // Getting databases should not create files on disk.
    RetainPtr appleURL = [NSURL URLWithString:@"http://apple.com"];
    RetainPtr webkitURL = [NSURL URLWithString:@"webkit://"];
    __block RetainPtr<NSString> appleDirectoryString;
    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] _originDirectoryForTesting:appleURL.get() topOrigin:appleURL.get() type:WKWebsiteDataTypeIndexedDBDatabases completionHandler:^(NSString *result) {
        appleDirectoryString = result;
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);
    RetainPtr appleDirectoryURL = [NSURL fileURLWithPath:appleDirectoryString.get() isDirectory:YES];

    readyToContinue = false;
    __block RetainPtr<NSString> webkitDirectoryString;
    [[WKWebsiteDataStore defaultDataStore] _originDirectoryForTesting:webkitURL.get() topOrigin:webkitURL.get() type:WKWebsiteDataTypeIndexedDBDatabases completionHandler:^(NSString *result) {
        webkitDirectoryString = result;
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);
    RetainPtr webkitDirectoryURL = [NSURL fileURLWithPath:webkitDirectoryString.get() isDirectory:YES];

    __block RetainPtr<NSString> appleWebkitDirectoryString;
    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] _originDirectoryForTesting:webkitURL.get() topOrigin:appleURL.get() type:WKWebsiteDataTypeIndexedDBDatabases completionHandler:^(NSString *result) {
        appleWebkitDirectoryString = result;
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);
    RetainPtr appleWebkitDirectoryURL = [NSURL fileURLWithPath:appleWebkitDirectoryString.get() isDirectory:YES];

    RetainPtr defaultFileManager = [NSFileManager defaultManager];
    EXPECT_TRUE([defaultFileManager fileExistsAtPath:appleDirectoryURL.get().path]);
    EXPECT_FALSE([defaultFileManager fileExistsAtPath:appleWebkitDirectoryURL.get().path]);
    EXPECT_FALSE([defaultFileManager fileExistsAtPath:webkitDirectoryURL.get().path]);
}

static NSString *openFileHTMLString = @"<input style='width: 100vw; height: 100vh;' id='file' type='file'> \
    <script> \
    var database = null; \
    var selectedFile = null; \
    function fileSelected(event) { \
        selectedFile = event.target.files[0]; \
        if (!database) { \
            window.webkit.messageHandlers.testHandler.postMessage('Error: no database'); \
            return; \
        } \
        const transaction = database.transaction('TestObjectStore', 'readwrite'); \
        const objectStore = transaction.objectStore('TestObjectStore'); \
        objectStore.put({ }); \
        const request = objectStore.put({ index:1, file: selectedFile }); \
        request.onsuccess = function() { \
            window.webkit.messageHandlers.testHandler.postMessage('Put'); \
        }; \
        request.onerror = function() { \
            window.webkit.messageHandlers.testHandler.postMessage('Error: put failed'); \
        }; \
    } \
    function indexGetAll() { \
        if (!database) { \
            window.webkit.messageHandlers.testHandler.postMessage('Error: no database'); \
            return; \
        } \
        const transaction = database.transaction('TestObjectStore'); \
        const objectStore = transaction.objectStore('TestObjectStore', 'readonly'); \
        const index = objectStore.index('index'); \
        const request = index.getAll(1); \
        request.onsuccess = function(event) { \
            const files = event.target.result; \
            if (files.length == 1) { \
                readFileAsText(files[0].file); \
            } else { \
                window.webkit.messageHandlers.testHandler.postMessage('Error: files length ' + files.length); \
            } \
        }; \
    } \
    function indexGet() { \
        if (!database) { \
            window.webkit.messageHandlers.testHandler.postMessage('Error: no database'); \
            return; \
        } \
        const transaction = database.transaction('TestObjectStore'); \
        const objectStore = transaction.objectStore('TestObjectStore', 'readonly'); \
        const index = objectStore.index('index'); \
        const request = index.get(IDBKeyRange.upperBound(1)); \
        request.onsuccess = function(event) { \
            readFileAsText(event.target.result.file); \
        }; \
    } \
    function readFileAsText(file) \
    { \
        const reader = new FileReader(); \
        reader.onload = function(event) { \
            window.webkit.messageHandlers.testHandler.postMessage(event.target.result); \
        }; \
        reader.readAsText(file); \
    } \
    const openRequest = indexedDB.open('StoreFile'); \
    openRequest.onupgradeneeded = function(event) { \
        database = event.target.result; \
        const objectStore = database.createObjectStore('TestObjectStore', { keyPath: 'id', autoIncrement: true }); \
        objectStore.createIndex('index', 'index'); \
    }; \
    openRequest.onsuccess = function(event) { \
        database = event.target.result; \
        window.webkit.messageHandlers.testHandler.postMessage('Opened'); \
    }; \
    const fileElement = document.getElementById('file'); \
    fileElement.onchange = fileSelected; \
    </script>";

TEST(IndexedDB, GetFileByIndex)
{
    readyToContinue = false;
    [[WKWebsiteDataStore defaultDataStore] removeDataOfTypes:[NSSet setWithObjects:WKWebsiteDataTypeIndexedDBDatabases, nil] modifiedSince:[NSDate distantPast] completionHandler:^() {
        readyToContinue = true;
    }];
    TestWebKitAPI::Util::run(&readyToContinue);

    RetainPtr<IndexedDBMessageHandler> handler = adoptNS([[IndexedDBMessageHandler alloc] init]);
    RetainPtr<WKWebViewConfiguration> configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    RetainPtr webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100) configuration:configuration.get()]);
    auto uiDelegate = adoptNS([[IndexedDBOpenPanelUIDelegate alloc] init]);
    [webView setUIDelegate:uiDelegate.get()];
    [webView loadHTMLString:openFileHTMLString baseURL:[NSURL URLWithString:@"https://webkit.org"]];
    EXPECT_WK_STREQ(@"Opened", [getNextMessage() body]);

    [webView clickOnElementID:@"file"];
    EXPECT_WK_STREQ(@"Put", [getNextMessage() body]);

    EXPECT_NOT_NULL(selectedFilePath);
    // Remove file on disk to validate that database stores a copy.
    [[NSFileManager defaultManager] removeItemAtPath:selectedFilePath.get() error:nil];
    [webView _killWebContentProcessAndResetState];

    [webView loadHTMLString:openFileHTMLString baseURL:[NSURL URLWithString:@"https://webkit.org"]];
    EXPECT_WK_STREQ(@"Opened", [getNextMessage() body]);

    [webView evaluateJavaScript:@"indexGetAll()" completionHandler:nil];
    EXPECT_WK_STREQ(@"This is test file", [getNextMessage() body]);

    [webView evaluateJavaScript:@"indexGet()" completionHandler:nil];
    EXPECT_WK_STREQ(@"This is test file", [getNextMessage() body]);
}
