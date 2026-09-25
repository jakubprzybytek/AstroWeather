// The status a client fetch reports for each first-failure stage. Every
// stage name St67NetworkSession and St67HttpFetchTask pass to fail() is
// listed; several map to HttpFailure although nothing HTTP went wrong, which is
// current behaviour (docs/WiFi.md, "Failure mapping").

#include <WiFi/St67FetchStatusMap.hpp>

#include <Expect.hpp>

#include <string>

namespace {

using namespace HostController;
using Test::expectEqual;

St67FetchStatus statusFor(const char* stage, bool responseTooLarge = false)
{
    // A copy, so the mapping is shown to compare names, not pointers.
    const std::string copy = stage;
    return fetchStatusForFailure(copy.c_str(), responseTooLarge);
}

void testSuccess()
{
    expectEqual(fetchStatusForFailure(nullptr, false), St67FetchStatus::Success,
                "no failed stage is Success");
    // Current behaviour: with no failed stage the overflow flag is not looked at
    // (the fetcher's data callback fails the fetch whenever it sets it).
    expectEqual(fetchStatusForFailure(nullptr, true), St67FetchStatus::Success,
                "no failed stage is Success even with the overflow flag");
}

void testMappedStages()
{
    expectEqual(statusFor("credentials"), St67FetchStatus::NoCredentials, "credentials");
    expectEqual(statusFor("w6x-init"), St67FetchStatus::DriverFailure, "w6x-init");
    expectEqual(statusFor("wifi-init"), St67FetchStatus::DriverFailure, "wifi-init");
    expectEqual(statusFor("connect"), St67FetchStatus::NetworkFailure, "connect");
    expectEqual(statusFor("connect-state"), St67FetchStatus::NetworkFailure, "connect-state");
    expectEqual(statusFor("dhcp"), St67FetchStatus::NetworkFailure, "dhcp");
    expectEqual(statusFor("fetch"), St67FetchStatus::HttpFailure, "fetch");
}

void testKnownGaps()
{
    // Current behaviour: driver and network-stack set-up failures other than
    // w6x-init/wifi-init report HttpFailure, not DriverFailure.
    expectEqual(statusFor("module-info"), St67FetchStatus::HttpFailure, "module-info");
    expectEqual(statusFor("callback-register"), St67FetchStatus::HttpFailure,
                "callback-register");
    expectEqual(statusFor("lwip-init"), St67FetchStatus::HttpFailure, "lwip-init");
    expectEqual(statusFor("lwip-netif"), St67FetchStatus::HttpFailure, "lwip-netif");
    // Current behaviour: a failed disconnect after the body arrived turns the
    // whole fetch into HttpFailure.
    expectEqual(statusFor("disconnect"), St67FetchStatus::HttpFailure, "disconnect");
    expectEqual(statusFor("link-down"), St67FetchStatus::HttpFailure, "link-down");
    expectEqual(statusFor("reconnect"), St67FetchStatus::HttpFailure, "reconnect");
    expectEqual(statusFor("persistent-ready"), St67FetchStatus::HttpFailure,
                "persistent-ready");
    expectEqual(statusFor("some-new-stage"), St67FetchStatus::HttpFailure,
                "an unknown stage");
}

void testCleanupFailure()
{
    // Current behaviour: CleanupFailure is unreachable for clients. final-state
    // is only set by stop(), which a client fetch calls only after
    // initialize() has already recorded the first failure, and nothing sets
    // netif-stop any more.
    expectEqual(statusFor("final-state"), St67FetchStatus::CleanupFailure, "final-state");
    expectEqual(statusFor("netif-stop"), St67FetchStatus::CleanupFailure, "netif-stop");
}

void testResponseTooLarge()
{
    expectEqual(statusFor("fetch", true), St67FetchStatus::ResponseTooLarge,
                "body overflowed the caller's buffer");
    // Current behaviour: the overflow flag wins over whichever stage failed.
    expectEqual(statusFor("disconnect", true), St67FetchStatus::ResponseTooLarge,
                "overflow wins over a later disconnect failure");
    expectEqual(statusFor("credentials", true), St67FetchStatus::ResponseTooLarge,
                "overflow wins over any stage");
    // Current behaviour: a Content-Length over 4096 is refused by
    // HttpClient_Get() before any body reaches the fetcher's data callback, the
    // only place the overflow flag is set, so it shows as a plain fetch failure.
    expectEqual(statusFor("fetch", false), St67FetchStatus::HttpFailure,
                "Content-Length over 4096 is HttpFailure, not ResponseTooLarge");
}

void testNeverProduced()
{
    const char* stages[] = {"credentials", "w6x-init", "module-info", "callback-register",
                            "wifi-init", "lwip-init", "lwip-netif", "connect",
                            "connect-state", "dhcp", "disconnect", "link-down", "reconnect",
                            "final-state", "netif-stop", "fetch", "persistent-ready"};
    for (const char* stage : stages) {
        for (bool tooLarge : {false, true}) {
            const St67FetchStatus status = statusFor(stage, tooLarge);
            // Busy, InvalidArgument and Timeout are set by the caller's side.
            Test::expect(status != St67FetchStatus::Busy &&
                             status != St67FetchStatus::InvalidArgument &&
                             status != St67FetchStatus::Timeout &&
                             status != St67FetchStatus::Success,
                         "a failed stage never maps to a caller-side status or Success");
        }
    }
}

} // namespace

int main()
{
    testSuccess();
    testMappedStages();
    testKnownGaps();
    testCleanupFailure();
    testResponseTooLarge();
    testNeverProduced();
    return Test::finish("St67FetchStatusMap");
}
