#ifndef INC_HOSTCONTROLLER_TRUSTEDCA_HPP_
#define INC_HOSTCONTROLLER_TRUSTEDCA_HPP_

// The trust anchor for the astro API. The API is served on an API Gateway
// custom domain with an ACM certificate (CloudFront until October 2026; same
// chain), whose default chain ends in Amazon Root CA 1 (RSA 2048,
// valid to 2038-01-17). Public data, so it lives in source; the Wi-Fi password
// does not. If ACM ever changes the chain, add the new root here and record
// the rotation in Docs/archive/ST67_HTTPS_Implementation_Plan.md.
//
// A bench build configured with -DAPP_ST67_TLS_BENCH_ANCHOR_ISRG=ON trusts
// ISRG Root X1 instead, so that the badssl.com test hosts can be fetched and
// the production API becomes the wrong-CA case; see the HTTPS plan, section 8.

namespace HostController {
namespace TrustedCa {

// File name the certificate is stored under in the module's file system. Up to
// 31 characters. The module keeps the file; the driver compares it with the
// PEM below before each request and rewrites it only when they differ.
extern const char kAnchorName[];

// PEM text, NUL-terminated, CRLF line endings as in ST's examples.
// Production: Amazon Root CA 1, SHA-256 fingerprint 8E:CD:E6:88:4F:3D:87:B1:
// 12:5B:A3:1A:C3:FC:B1:3D:70:16:DE:7F:57:CC:90:4F:E1:CB:97:C6:AE:98:19:6E,
// from https://www.amazontrust.com/repository/AmazonRootCA1.pem (2026-10-03).
extern const char kAnchorPem[];

}  // namespace TrustedCa
}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_TRUSTEDCA_HPP_ */
