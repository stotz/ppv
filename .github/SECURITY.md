# Security policy

ppv is a small command line tool without network access, configuration files
or privileges. Its attack surface is the command line and the data stream it
copies, which it treats as opaque bytes.

## Supported versions

Only the latest release is supported. Fixes are published as a new release.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private
vulnerability reporting on this repository ("Security" tab, "Report a
vulnerability"). Include the ppv version (`ppv --version`), the platform, and a
way to reproduce the problem. This is a one-person project; reports are read
and answered, but there is no guaranteed response time.

## Verifying downloads

Every release archive carries a signed build provenance attestation:

```sh
gh attestation verify ppv-X.Y.Z-linux-x64.tar.gz -R stotz/ppv
sha256sum -c SHA256SUMS --ignore-missing
```
