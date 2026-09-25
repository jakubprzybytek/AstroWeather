// The fetcher's configuration check (host and path) and its Content-Type
// check. Cases marked "current behaviour" pin what the code does today, not
// what HTTP allows.

#include <WiFi/St67HttpRules.hpp>

#include <Expect.hpp>

#include <string>

namespace {

using namespace HostController::St67HttpRules;
using Test::expect;

constexpr size_t kMaxHost = 64U;  // HTTP_SNI_MAX_SIZE
constexpr char kExpected[] = "text/plain; charset=utf-8";  // APP_ST67_HTTP_EXPECTED_CONTENT_TYPE

bool validHost(const std::string& host)
{
    return isValidTarget(host.c_str(), "/astro/wroclaw", kMaxHost);
}

bool validPath(const std::string& path)
{
    return isValidTarget("api.example.com", path.c_str(), kMaxHost);
}

ContentTypeCheck check(const std::string& headers)
{
    return checkContentType(reinterpret_cast<const uint8_t*>(headers.data()),
                            static_cast<uint16_t>(headers.size()), kExpected);
}

std::string headersWith(const std::string& contentTypeLine)
{
    return "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n" + contentTypeLine + "\r\n\r\n";
}

void testValidHosts()
{
    expect(isValidTarget("api.int.astroweather.albedoonline.com", "/astro/wroclaw", kMaxHost),
           "the configured host and path");
    expect(validHost("example.com"), "a short host");
    expect(validHost("192.168.1.10"), "an IPv4 address");
    expect(validHost(std::string(64U, 'a')), "a 64-character host");
}

void testInvalidHosts()
{
    expect(!validHost(""), "empty host");
    expect(!validHost(std::string(65U, 'a')), "host longer than the SNI limit");
    expect(!validHost("http://example.com"), "host with a scheme");
    expect(!validHost("https://example.com"), "host with an https scheme");
    expect(!validHost("example.com:8080"), "host with a port");
    expect(!validHost("[::1]"), "IPv6 literal (has colons)");
    expect(!validHost("example.com\r"), "host with CR");
    expect(!validHost("example.com\nX: y"), "host with LF");
    expect(!validHost("exa mple.com"), "host with a space");
    expect(!validHost("example.com\t"), "host with a tab");
    expect(!validHost("example.com/astro"), "host with a path");
}

void testPaths()
{
    expect(validPath("/"), "root path");
    expect(validPath("/astro/wroclaw"), "the configured path");
    expect(validPath("/astro?city=wroclaw&days=2"), "path with a query");
    expect(!validPath("/a b"), "path with a space");
    expect(!validPath("/a\tb"), "path with a tab");
    expect(!validPath(""), "empty path");
    expect(!validPath("astro/wroclaw"), "path without a leading slash");
    expect(!validPath("http://example.com/astro"), "absolute URL as the path");
    expect(!validPath("/astro\r\nHost: evil"), "path with CR/LF");
    expect(!validPath("/astro\n"), "path with LF");
}

void testContentTypeMatches()
{
    expect(check(headersWith("Content-Type: text/plain; charset=utf-8")) ==
               ContentTypeCheck::Match,
           "exact Content-Type");
    expect(check(headersWith("Content-Type:text/plain; charset=utf-8")) ==
               ContentTypeCheck::Match,
           "no space after the colon");
    expect(check(headersWith("Content-Type: \t  text/plain; charset=utf-8")) ==
               ContentTypeCheck::Match,
           "extra spaces and tabs after the colon");
    expect(check(headersWith("Content-Type: text/plain; charset=utf-8; format=flowed")) ==
               ContentTypeCheck::Match,
           "extra parameters after the expected ones");
    // Current behaviour: a prefix match, so anything may follow.
    expect(check(headersWith("Content-Type: text/plain; charset=utf-8x")) ==
               ContentTypeCheck::Match,
           "a longer charset still matches the prefix");
    expect(check("Content-Type: text/plain; charset=utf-8") == ContentTypeCheck::Match,
           "value running to the end of the headers");
}

void testContentTypeMismatches()
{
    expect(check(headersWith("Content-Type: text/plain")) == ContentTypeCheck::Mismatch,
           "no charset parameter");
    expect(check(headersWith("Content-Type: application/json")) == ContentTypeCheck::Mismatch,
           "a different type");
    // Current behaviour: parameters are compared byte for byte, so spacing and
    // case must be exactly as expected.
    expect(check(headersWith("Content-Type: text/plain;charset=utf-8")) ==
               ContentTypeCheck::Mismatch,
           "no space after the semicolon");
    expect(check(headersWith("Content-Type: text/plain;  charset=utf-8")) ==
               ContentTypeCheck::Mismatch,
           "two spaces after the semicolon");
    expect(check(headersWith("Content-Type: text/plain; charset=UTF-8")) ==
               ContentTypeCheck::Mismatch,
           "upper-case charset value");
    expect(check(headersWith("Content-Type: Text/Plain; charset=utf-8")) ==
               ContentTypeCheck::Mismatch,
           "mixed-case media type");
    expect(check("Content-Type: text/pl") == ContentTypeCheck::Mismatch,
           "value cut off by the header length");
}

void testContentTypeMissing()
{
    expect(check("") == ContentTypeCheck::Missing, "empty headers");
    expect(check(headersWith("X-Other: 1")) == ContentTypeCheck::Missing, "no Content-Type");
    // HTTP headers are case-insensitive; see docs/Testing.md known issues.
    // Current behaviour: the header name must be spelled exactly.
    expect(check(headersWith("content-type: text/plain; charset=utf-8")) ==
               ContentTypeCheck::Missing,
           "lower-case header name is not found");
    expect(check(headersWith("CONTENT-TYPE: text/plain; charset=utf-8")) ==
               ContentTypeCheck::Missing,
           "upper-case header name is not found");
    expect(check(headersWith("Content-Type : text/plain; charset=utf-8")) ==
               ContentTypeCheck::Missing,
           "space before the colon");

    // The search stops at the given length, so a body is never looked at.
    const std::string headers = "HTTP/1.1 200 OK\r\n\r\n";
    const std::string withBody = headers + "Content-Type: text/plain; charset=utf-8";
    expect(checkContentType(reinterpret_cast<const uint8_t*>(withBody.data()),
                            static_cast<uint16_t>(headers.size()),
                            kExpected) == ContentTypeCheck::Missing,
           "Content-Type past the header length is ignored");
}

void testFirstOccurrenceWins()
{
    // Current behaviour: a plain substring search, so the first "Content-Type:"
    // anywhere in the headers is used, even inside another header.
    expect(check("HTTP/1.1 200 OK\r\nX-Content-Type: text/html\r\n"
                 "Content-Type: text/plain; charset=utf-8\r\n\r\n") ==
               ContentTypeCheck::Mismatch,
           "X-Content-Type: is matched first");
    expect(check("HTTP/1.1 200 OK\r\nX-Content-Type-Options: nosniff\r\n"
                 "Content-Type: text/plain; charset=utf-8\r\n\r\n") == ContentTypeCheck::Match,
           "X-Content-Type-Options does not contain the name with its colon");
    expect(check("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                 "Content-Type: text/html\r\n\r\n") == ContentTypeCheck::Match,
           "a second Content-Type is not looked at");
}

} // namespace

int main()
{
    testValidHosts();
    testInvalidHosts();
    testPaths();
    testContentTypeMatches();
    testContentTypeMismatches();
    testContentTypeMissing();
    testFirstOccurrenceWins();
    return Test::finish("St67HttpRules");
}
