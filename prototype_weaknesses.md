A list weaknesses with the prototype and improvements that should/could be made
to inform the real implementation. Use this document as a reference to add items
to (ROADMAP.md)[./ROADMAP.md].

- Using a self-signed cert as a shared secret is overkill. Some other protocol
  that is specifically designed for machine-to-machine communication would be
  nicer such as TLS-PSK. Using a preshared private key would also mean that the
  key data and consequentailly the invite can be a lot smaller
- The prototype assumes that the invite, containing TLS cert is exchanged over
  as secure channel. We should provide the host and guest additional security
  features to protect them in situations where the secure channel is
  compromised. Some methods that I can think of are:
  - Time-limited PIN codes that would be exchanged over a 2nd channel, such as
    voice chat or in-person communication
  - Use existing public key technologies to encrypt invites. For example, by
    using trusted PGP public keys
  - OAuth2 (sigh) but that's going to be time consuming to set up properly
    without major 3rd party dependency bloat
- The host should have the option to set the placeholder for the host IP to a
  specific value. If possible, without additional 3rd-party dependencies, we
  should precompute the public host IP, and network local IP and let the host
  select them from an options list, obviously while maintaining the option to
  supply a custom value. Maybe this options list screen should also contain
  information on home-router configuration or point to relevant resources
- The host IP placeholder should come last in the invite so it's more ergonomic
  for the guest to change
- There should be an option to send a curl and/or wget command as an invite
  that will download the guest executable first for guests that don't have
  `termc` preinstalled. Maybe the socket sever should serve a HTML page with
  information for guests?
- We should enforce access control for guests and have some role presets:
  - Admin
  - Editor
  - Viewer that can suggest input
  - Viewer that is completely silent
- Banning or changing a guest's access should be easy
- A host should be able to allow a guest to switch to their terminal and back
- Guests should only be running a pty when necessary. Currently the guests
  create a pty. This is a bug in the prototype
- All I/O should be non-blocking
- We should forward output to guests in parallel
- There should be a maximum wait timeout on forwarding output to guests. The
  host should drop packets to guests that are to slow to avoid a degraded
  experience for other guests.
- Admins should be able to attach to a admin CLI in a separate terminal
- All participants should be able to attach to a chat CLI in a separate
  terminal
- Possibly we should support multiple parallel sessions through the same host?
- There should be a time window in which input and output is buffered before
  being transmitted so less packets need to be sent
- The prototype may be susceptible to sidechannel attacks: it is possible that
  an attacker can gain valuable information by heuristically analysing the
  length and frequency of encrypted packets. We should research security issues
  that the OpenSSH project has encountered and mitigate similar issues in our
  implementation
- Maybe each guest should be handled in a separate thread/process as an
  isolation boundary for extra security?


