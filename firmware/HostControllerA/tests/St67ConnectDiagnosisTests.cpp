// Classifying a failed station connect from the module's reason code and an
// SSID scan, and the plain-language line logged for each outcome.

#include <WiFi/St67ConnectDiagnosis.hpp>

#include <Expect.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace HostController;
using namespace HostController::St67ConnectDiagnosis;
using Test::expect;
using Test::expectEqual;

constexpr char kSsid[] = "HomeNet";
constexpr char kPassword[] = "s3cret-Passw0rd";

// Renders the log line the way St67NetworkSession's reportConnectFailure()
// does, with the arguments failureMessageArguments() names.
std::string render(WifiConnectResult result, const char* reasonText = "BEACON_LOSS",
                   unsigned long reason = 16UL)
{
    char line[256];
    const char* format = failureMessageFormat(result);
    switch (failureMessageArguments(result)) {
    case MessageArguments::None:
        std::snprintf(line, sizeof(line), "%s", format);
        break;
    case MessageArguments::Ssid:
        std::snprintf(line, sizeof(line), format, kSsid);
        break;
    case MessageArguments::SsidAndReason:
        std::snprintf(line, sizeof(line), format, kSsid, reasonText, reason);
        break;
    }
    return line;
}

// Conversions in a printf format, not counting "%%".
int conversions(const char* format)
{
    int count = 0;
    for (const char* at = std::strchr(format, '%'); at != nullptr;
         at = std::strchr(at + 2, '%')) {
        if (at[1] != '%') {
            ++count;
        }
    }
    return count;
}

bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

void testReasonCodes()
{
    expect(classifyConnectFailure(kNoReason) == WifiConnectResult::NoResponse,
           "timeout without a reason is NoResponse");
    expect(classifyConnectFailure(12U) == WifiConnectResult::NetworkNotFound,
           "no BSSID and channel is NetworkNotFound");
    expect(classifyConnectFailure(7U) == WifiConnectResult::WrongPassword,
           "deauth during connection is WrongPassword");
    expect(classifyConnectFailure(8U) == WifiConnectResult::WrongPassword,
           "PSK handshake timeout is WrongPassword");
    expect(classifyConnectFailure(2U) == WifiConnectResult::SecurityMismatch,
           "authentication failure is SecurityMismatch");
    expect(classifyConnectFailure(3U) == WifiConnectResult::SecurityMismatch,
           "auth algorithm failure is SecurityMismatch");
    expect(classifyConnectFailure(17U) == WifiConnectResult::SecurityMismatch,
           "security no-match is SecurityMismatch");

    // Everything else, including WLAN_FW_SUCCESSFUL (0) on a failed connect.
    const uint32_t others[] = {0U, 1U, 4U, 5U, 6U, 9U, 10U, 11U, 13U, 14U, 15U, 16U, 18U,
                               19U, 20U, 21U, 22U, 1000U, 0xFFFFFFFEU};
    for (uint32_t reason : others) {
        expect(classifyConnectFailure(reason) == WifiConnectResult::Failed,
               "other reason codes are Failed");
    }
    expectEqual(kReasonNoBssidAndChannel, 12U, "reason code 12");
}

void testScanFallback()
{
    const WifiConnectResult classified = classifyConnectFailure(kNoReason);
    expect(needsScan(classified), "a silent timeout needs a scan");
    expect(applyScan(classified, SsidVisibility::NotVisible) ==
               WifiConnectResult::NetworkNotFound,
           "scan did not find the SSID");
    expect(applyScan(classified, SsidVisibility::Visible) == WifiConnectResult::NoResponse,
           "scan found the SSID");
    expect(applyScan(classified, SsidVisibility::Unknown) ==
               WifiConnectResult::NoResponseUnchecked,
           "scan could not run");

    const WifiConnectResult withReason[] = {
        WifiConnectResult::NetworkNotFound, WifiConnectResult::WrongPassword,
        WifiConnectResult::SecurityMismatch, WifiConnectResult::Failed};
    for (WifiConnectResult result : withReason) {
        expect(!needsScan(result), "a reason code needs no scan");
        expect(applyScan(result, SsidVisibility::NotVisible) == result,
               "a scan does not change a result with a reason");
    }
}

void testMessages()
{
    expectEqual(render(WifiConnectResult::NoCredentials),
                std::string("WiFi not configured: no SSID stored. Set one with 'wifi set "
                            "<ssid> <password>'."),
                "no credentials");
    expectEqual(render(WifiConnectResult::NetworkNotFound),
                std::string("WiFi network 'HomeNet' not found: no access point with that name "
                            "is in range. Check the SSID; it is case-sensitive."),
                "network not found");
    expectEqual(render(WifiConnectResult::WrongPassword),
                std::string("WiFi 'HomeNet' rejected the connection during the password "
                            "check, which almost always means a wrong password. Re-enter it "
                            "with 'wifi set'."),
                "wrong password");
    expectEqual(render(WifiConnectResult::SecurityMismatch),
                std::string("WiFi 'HomeNet' refused authentication. Check the password, and "
                            "that the network uses WPA2 (or is open when no password is "
                            "set)."),
                "security mismatch");
    expectEqual(render(WifiConnectResult::NoResponse),
                std::string("WiFi 'HomeNet' is in range but did not answer the connection "
                            "request. Try again, or restart the access point."),
                "no response");
    expectEqual(render(WifiConnectResult::NoResponseUnchecked),
                std::string("WiFi 'HomeNet' did not answer before the connect timeout. Check "
                            "the SSID (it is case-sensitive) and that the access point is on "
                            "and in range."),
                "no response, unchecked");
    expectEqual(render(WifiConnectResult::DhcpFailed),
                std::string("WiFi joined 'HomeNet' but got no IP address from DHCP. Check "
                            "the router's DHCP server."),
                "DHCP failed");
    expectEqual(render(WifiConnectResult::Failed),
                std::string("WiFi 'HomeNet' connect failed: BEACON_LOSS (reason 16)."),
                "other failure");
    // The session passes "no reason given" when no reason event arrived.
    expectEqual(render(WifiConnectResult::Failed, "no reason given", 0xFFFFFFFFUL),
                std::string("WiFi 'HomeNet' connect failed: no reason given (reason "
                            "4294967295)."),
                "other failure without a reason");
    // Current behaviour: results that are not failures fall to the generic line.
    expect(failureMessageArguments(WifiConnectResult::Connected) ==
               MessageArguments::SsidAndReason,
           "Connected uses the generic line");
    expect(failureMessageArguments(WifiConnectResult::NeverTried) ==
               MessageArguments::SsidAndReason,
           "NeverTried uses the generic line");
}

void testPasswordNeverLogged()
{
    const WifiConnectResult results[] = {
        WifiConnectResult::NeverTried,       WifiConnectResult::Connected,
        WifiConnectResult::NoCredentials,    WifiConnectResult::NetworkNotFound,
        WifiConnectResult::WrongPassword,    WifiConnectResult::SecurityMismatch,
        WifiConnectResult::NoResponse,       WifiConnectResult::NoResponseUnchecked,
        WifiConnectResult::DhcpFailed,       WifiConnectResult::Failed,
    };
    for (WifiConnectResult result : results) {
        const char* format = failureMessageFormat(result);
        int expected = 0;
        switch (failureMessageArguments(result)) {
        case MessageArguments::None: expected = 0; break;
        case MessageArguments::Ssid: expected = 1; break;
        case MessageArguments::SsidAndReason: expected = 3; break;
        }
        // Every conversion is accounted for by the SSID or the reason, so there
        // is no slot a password could be formatted into.
        expectEqual(conversions(format), expected, "format takes only the listed arguments");
        const std::string line = render(result);
        expect(!contains(line, kPassword), "the password is never in the message");
        if (result != WifiConnectResult::NoCredentials) {
            expect(contains(line, kSsid), "the SSID is named");
        }
    }
}

} // namespace

int main()
{
    testReasonCodes();
    testScanFallback();
    testMessages();
    testPasswordNeverLogged();
    return Test::finish("St67ConnectDiagnosis");
}
