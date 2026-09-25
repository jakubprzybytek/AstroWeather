// The socket-free half of HttpClient_Get(): header end, status line,
// Content-Length, the 2048-byte header buffer and the body limits. Several
// cases pin current behaviour rather than what HTTP allows; they say so.

#include <WiFi/HttpResponseParser.hpp>

#include <Expect.hpp>

#include <string>
#include <vector>

namespace {

using namespace HostController::HttpResponse;
using Test::expect;
using Test::expectEqual;

constexpr uint32_t kMaxResponse = 4096U;  // APP_ST67_HTTP_MAX_RESPONSE_BYTES

const uint8_t* bytes(const std::string& text)
{
    return reinterpret_cast<const uint8_t*>(text.c_str());
}

// Parses a complete head the way HttpClient_Get() does: up to the blank line.
bool parse(const std::string& text, Head& head)
{
    uint32_t end = 0U;
    if (!findHeaderEnd(bytes(text), static_cast<uint32_t>(text.size()), &end)) {
        return false;
    }
    return parseHead(bytes(text), end, &head);
}

// The header buffer HttpClient_Get() heap-allocates, fed one read at a time.
struct HeaderReader {
    std::vector<uint8_t> buffer = std::vector<uint8_t>(kHeaderCapacity + 1U, 0U);
    uint32_t length = 0U;
    Head head{};
    uint32_t headEnd = 0U;
    uint32_t bodyOffset = 0U;

    HeaderProgress feed(const std::string& chunk, uint32_t maxResponse = kMaxResponse)
    {
        return appendHeaderBytes(buffer.data(), kHeaderCapacity, &length, bytes(chunk),
                                 static_cast<uint32_t>(chunk.size()), maxResponse, &head,
                                 &headEnd, &bodyOffset);
    }
};

// Status line plus the given header lines and the blank line.
std::string response(const std::string& statusLine, const std::string& headers = "")
{
    return statusLine + "\r\n" + headers + "\r\n";
}

void testHeaderEnd()
{
    uint32_t offset = 0U;
    const std::string text = "HTTP/1.1 200 OK\r\n\r\nbody";
    expect(findHeaderEnd(bytes(text), static_cast<uint32_t>(text.size()), &offset),
           "blank line found");
    expectEqual(offset, 19U, "body starts after the blank line");

    const std::string partial = "HTTP/1.1 200 OK\r\n\r";
    expect(!findHeaderEnd(bytes(partial), static_cast<uint32_t>(partial.size()), &offset),
           "an incomplete blank line is not the end");
    expect(!findHeaderEnd(bytes("\r\n\r"), 3U, &offset), "fewer than 4 bytes");
    expect(findHeaderEnd(bytes("\r\n\r\n"), 4U, &offset) && offset == 4U,
           "exactly the terminator");
    // Current behaviour: bare LF line endings are not recognised.
    const std::string bareLf = "HTTP/1.1 200 OK\n\nbody";
    expect(!findHeaderEnd(bytes(bareLf), static_cast<uint32_t>(bareLf.size()), &offset),
           "LF-only headers never end");
}

void testHeaderEndSplitAcrossReads()
{
    HeaderReader reader;
    const std::string first = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r";
    expect(reader.feed(first) == HeaderProgress::NeedMore, "first half needs more");
    expect(reader.feed("\nhi") == HeaderProgress::Complete, "terminator split across reads");
    expectEqual(reader.headEnd, static_cast<uint32_t>(first.size()) + 1U,
                "head ends inside the second read");
    expectEqual(reader.bodyOffset, 1U, "body starts after the split terminator");
    expectEqual(reader.head.contentLength, 2U, "Content-Length from the first read");

    HeaderReader sameRead;
    const std::string head = response("HTTP/1.1 200 OK", "Content-Length: 5\r\n");
    expect(sameRead.feed(head + "hello") == HeaderProgress::Complete, "head and body together");
    expectEqual(sameRead.bodyOffset, static_cast<uint32_t>(head.size()),
                "body offset within the read");
    expectEqual(sameRead.headEnd, static_cast<uint32_t>(head.size()), "head length");

    HeaderReader thirdRead;
    expect(thirdRead.feed("HTTP/1.1 2") == HeaderProgress::NeedMore, "status split, part 1");
    expect(thirdRead.feed("00 OK\r\nX: y\r\n") == HeaderProgress::NeedMore, "status split, part 2");
    expect(thirdRead.feed("\r\nab") == HeaderProgress::Complete, "status split, part 3");
    expectEqual(thirdRead.head.statusCode, 200U, "status line split across reads");
    expectEqual(thirdRead.bodyOffset, 2U, "body offset in the third read");
}

void testStatusLine()
{
    const uint32_t codes[] = {200U, 204U, 301U, 404U, 500U};
    for (uint32_t code : codes) {
        Head head{};
        const bool ok = parse(response("HTTP/1.1 " + std::to_string(code) + " Reason"), head);
        expect(ok, "status line parses");
        expectEqual(head.statusCode, code, "status code");
    }

    Head head{};
    expect(parse(response("HTTP/1.0 200 OK"), head) && head.statusCode == 200U, "HTTP/1.0");
    head = Head{};
    expect(parse(response("HTTP/1.1 599 Custom"), head) && head.statusCode == 599U,
           "599 is the highest accepted");
    // Current behaviour: any number up to 599 is taken, even below 100.
    head = Head{};
    expect(parse(response("HTTP/1.1 099 Odd"), head) && head.statusCode == 99U,
           "a code below 100 is accepted");
    head = Head{};
    expect(parse(response("HTTP/1.1 200"), head), "reason phrase is optional");

    const char* malformed[] = {
        "HTTP/1.1 600 Too High",
        "HTTP/2 200",  // no minor version
        "HTTP/1.1 OK",
        "http/1.1 200 OK",
        "ICY 200 OK",
        "",
        "HTTP/1.1 -1 Negative",
    };
    for (const char* line : malformed) {
        Head rejected{};
        expect(!parse(response(line), rejected), "malformed status line rejected");
        expectEqual(rejected.statusCode, kNoStatus, "no status reported for a bad status line");
    }
}

void testAnySuccessStatus()
{
    expect(!isSuccessStatus(199U), "199 is not a success");
    expect(isSuccessStatus(200U), "200 is a success");
    expect(isSuccessStatus(204U), "204 is a success");
    expect(isSuccessStatus(206U), "206 is a success");
    expect(isSuccessStatus(299U), "299 is a success");
    expect(!isSuccessStatus(300U), "300 is not a success");
    expect(!isSuccessStatus(301U), "301 is not followed or accepted");
    expect(!isSuccessStatus(404U), "404 is not a success");
    expect(!isSuccessStatus(500U), "500 is not a success");
    expect(!isSuccessStatus(kNoStatus), "no status is not a success");
}

void testContentLength()
{
    Head head{};
    expect(parse(response("HTTP/1.1 200 OK", "Content-Type: text/plain\r\n"), head),
           "no Content-Length parses");
    expect(!head.hasContentLength && head.contentLength == 0U, "missing Content-Length");

    head = Head{};
    expect(parse(response("HTTP/1.1 204 No Content", "Content-Length: 0\r\n"), head),
           "zero Content-Length parses");
    expect(head.hasContentLength && head.contentLength == 0U, "zero Content-Length");
    expect(isBodyComplete(head, 0U), "a zero-length body is complete at once");

    head = Head{};
    expect(parse(response("HTTP/1.1 200 OK", "Content-Length:5\r\n"), head) &&
               head.contentLength == 5U,
           "no space after the colon");
    head = Head{};
    expect(parse(response("HTTP/1.1 200 OK", "Content-Length: \t 7\r\n"), head) &&
               head.contentLength == 7U,
           "spaces and tabs before the value");
    head = Head{};
    expect(parse(response("HTTP/1.1 200 OK", "Content-Length: 4294967295\r\n"), head) &&
               head.contentLength == 4294967295U,
           "largest 32-bit Content-Length");
    // Current behaviour: strtoul takes a leading '+'.
    head = Head{};
    expect(parse(response("HTTP/1.1 200 OK", "Content-Length: +5\r\n"), head) &&
               head.contentLength == 5U,
           "a leading plus sign is accepted");
    // Current behaviour: the last Content-Length wins.
    head = Head{};
    expect(parse(response("HTTP/1.1 200 OK", "Content-Length: 3\r\nContent-Length: 9\r\n"),
                 head) &&
               head.contentLength == 9U,
           "the last Content-Length wins");

    const char* malformed[] = {
        "Content-Length: abc\r\n",
        "Content-Length: \r\n",
        "Content-Length: 12abc\r\n",
        // Current behaviour: trailing whitespace is not allowed after the number.
        "Content-Length: 5 \r\n",
    };
    for (const char* line : malformed) {
        Head rejected{};
        expect(!parse(response("HTTP/1.1 200 OK", line), rejected),
               "malformed Content-Length rejected");
        // The status line had already parsed, so HttpClient_Get() still reports it.
        expectEqual(rejected.statusCode, 200U, "status kept for a bad Content-Length");
    }

    // A body line that looks like a header is not read.
    head = Head{};
    expect(parse("HTTP/1.1 200 OK\r\n\r\nContent-Length: 3\r\n", head) &&
               !head.hasContentLength,
           "Content-Length after the blank line is ignored");
}

void testContentLengthLimit()
{
    HeaderReader atLimit;
    expect(atLimit.feed(response("HTTP/1.1 200 OK", "Content-Length: 4096\r\n")) ==
               HeaderProgress::Complete,
           "Content-Length at the 4096 limit is accepted");

    HeaderReader overLimit;
    expect(overLimit.feed(response("HTTP/1.1 200 OK", "Content-Length: 4097\r\n")) ==
               HeaderProgress::TooLarge,
           "Content-Length over the 4096 limit is refused before the body");
    expectEqual(overLimit.head.statusCode, 200U, "status kept for a too-large response");

    // Values past 32 bits: strtoul saturates. With a 32-bit unsigned long (the
    // target, and MinGW) that is 4294967295, which is then too large; with a
    // 64-bit one (Linux CI) it is over UINT32_MAX and rejected as malformed.
    // Refused either way.
    const HeaderProgress hugeExpected = sizeof(unsigned long) == 4U
                                            ? HeaderProgress::TooLarge
                                            : HeaderProgress::Malformed;
    HeaderReader huge;
    expect(huge.feed(response("HTTP/1.1 200 OK", "Content-Length: 99999999999\r\n")) ==
               hugeExpected,
           "Content-Length past 32 bits is refused");
    HeaderReader negative;
    expect(negative.feed(response("HTTP/1.1 200 OK", "Content-Length: -1\r\n")) ==
               hugeExpected,
           "negative Content-Length wraps and is refused");

    HeaderReader nonNumeric;
    expect(nonNumeric.feed(response("HTTP/1.1 200 OK", "Content-Length: many\r\n")) ==
               HeaderProgress::Malformed,
           "non-numeric Content-Length is malformed");
}

void testHeaderNamesAreCaseSensitive()
{
    // HTTP headers are case-insensitive; see docs/Testing.md known issues.
    // Current behaviour: only the exact spelling "Content-Length:" counts.
    Head head{};
    expect(parse(response("HTTP/1.1 200 OK", "content-length: 5\r\n"), head) &&
               !head.hasContentLength,
           "lower-case content-length is ignored");
    head = Head{};
    expect(parse(response("HTTP/1.1 200 OK", "CONTENT-LENGTH: 5\r\n"), head) &&
               !head.hasContentLength,
           "upper-case CONTENT-LENGTH is ignored");
    expect(closeEndsBody(head), "so the body runs until the server closes");
}

void testChunkedTransferEncoding()
{
    // Current behaviour: Transfer-Encoding is not looked at. A chunked response
    // has no Content-Length, so its body, chunk markers included, is read until
    // the server closes the connection.
    HeaderReader reader;
    const std::string head =
        response("HTTP/1.1 200 OK", "Transfer-Encoding: chunked\r\nContent-Type: text/plain\r\n");
    const std::string body = "5\r\nhello\r\n0\r\n\r\n";
    expect(reader.feed(head + body) == HeaderProgress::Complete, "chunked head parses");
    expect(!reader.head.hasContentLength, "chunked has no Content-Length");
    expectEqual(reader.bodyOffset, static_cast<uint32_t>(head.size()),
                "chunk markers start the body");
    expect(acceptsBody(reader.head, 0U, static_cast<uint32_t>(body.size()), kMaxResponse),
           "chunk markers are accepted as body");
    expect(!isBodyComplete(reader.head, static_cast<uint32_t>(body.size())),
           "the terminating chunk does not end the body");
    expect(closeEndsBody(reader.head), "only the server closing ends it");
}

void testHeaderBufferLimit()
{
    const std::string status = "HTTP/1.1 200 OK\r\n";
    // A header line that brings the whole head to exactly 2048 bytes.
    const std::string fill =
        "X-Fill: " + std::string(kHeaderCapacity - status.size() - 8U - 4U, 'a') + "\r\n\r\n";
    const std::string exact = status + fill;
    expectEqual(exact.size(), static_cast<size_t>(kHeaderCapacity), "fixture is 2048 bytes");

    HeaderReader fits;
    expect(fits.feed(exact.substr(0U, 1024U)) == HeaderProgress::NeedMore, "first 1024 bytes");
    expect(fits.feed(exact.substr(1024U)) == HeaderProgress::Complete,
           "a 2048-byte head fits exactly");
    expectEqual(fits.bodyOffset, 1024U, "no body bytes after a full buffer");

    HeaderReader over;
    const std::string tooLong = status + "X-Fill: " +
                                std::string(kHeaderCapacity - status.size() - 8U - 3U, 'a') +
                                "\r\n\r\n";
    expect(over.feed(tooLong.substr(0U, 1024U)) == HeaderProgress::NeedMore, "first read");
    expect(over.feed(tooLong.substr(1024U)) == HeaderProgress::Overflow,
           "a 2049-byte head overflows");
    expectEqual(over.length, 1024U, "the overflowing read is not copied");

    // Current behaviour: body bytes in the same read as the blank line count
    // against the header buffer too.
    HeaderReader bodyOverflow;
    const std::string head = response(
        "HTTP/1.1 200 OK", "X-Fill: " + std::string(1500U, 'h') + "\r\nContent-Length: 600\r\n");
    expect(head.size() < kHeaderCapacity, "the head alone fits");
    expect(bodyOverflow.feed(head.substr(0U, 1000U)) == HeaderProgress::NeedMore,
           "head alone fits, first read");
    expect(bodyOverflow.feed(head.substr(1000U) + std::string(600U, 'b')) ==
               HeaderProgress::Overflow,
           "body bytes in the same read overflow the header buffer");
}

void testBodyLimits()
{
    Head sized{};
    sized.statusCode = 200U;
    sized.hasContentLength = true;
    sized.contentLength = 10U;
    expect(acceptsBody(sized, 0U, 10U, kMaxResponse), "body up to Content-Length");
    expect(acceptsBody(sized, 4U, 6U, kMaxResponse), "second part up to Content-Length");
    expect(!acceptsBody(sized, 5U, 6U, kMaxResponse), "body past Content-Length refused");
    expect(isBodyComplete(sized, 10U), "complete at Content-Length");
    expect(!isBodyComplete(sized, 9U), "one byte short is not complete");
    expect(!closeEndsBody(sized), "a close before Content-Length is a truncated response");

    Head unsized{};
    unsized.statusCode = 200U;
    expect(acceptsBody(unsized, 4000U, 96U, kMaxResponse), "up to 4096 without Content-Length");
    expect(!acceptsBody(unsized, 4000U, 97U, kMaxResponse), "past 4096 refused");
    expect(!isBodyComplete(unsized, 4096U), "never complete by length");
    expect(closeEndsBody(unsized), "ended by the server closing");
}

} // namespace

int main()
{
    testHeaderEnd();
    testHeaderEndSplitAcrossReads();
    testStatusLine();
    testAnySuccessStatus();
    testContentLength();
    testContentLengthLimit();
    testHeaderNamesAreCaseSensitive();
    testChunkedTransferEncoding();
    testHeaderBufferLimit();
    testBodyLimits();
    return Test::finish("HttpResponseParser");
}
