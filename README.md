# `termc` -- Terminal Collaboration

`termc` is a plain old Unix CLI application for sharing you terminal with
others. Running `termc` as a host starts a simple E2E encrypted socket server
allowing you to share your terminal's I/O with guests that connect to the
session that you're hosting.

Currently a WIP!


## Release and Versioning Plan

I plan on releasing `1.0` when I am relatively happy with the security of the
software. I may possibly release prereleases `0.1` and `0.2` before `1.0` but
they're primarily milestones and I might be too lazy to create an actual
release.

The requirements for `0.1`, `0.2` and `1.0` are:
- `0.1`: 
    1. **Packets between the host and guests are enctypted**
    2. The sofware works with a small feature set
- `0.2`:
    1. **I'm motivated to do security hardening**
    2. The feature set is more complete
    3. The sofware still works
- `1.0`:
    1. **Look for missing error state handling, bounds-checking, etc. and fix**
    2. **Replace some unsafe C operations with safer operations where
       performance isn't critical, e.g. `safe_deref(ptr)`,
       `safe_set(buf, idx, val)`, etc.**
    3. **Past OpenSSH (and possibly telnet) security vulnerabilities have been
       researched and similar issues in the software have been fixed or
       mitigated.**

`1.0` will likely only have automated tests where they help me tackle
implementation complexity; most testing will likely be manual. If this becomes
a long-term project, automated E2E and unit testing will be a focus to increase
the reliability of well-established, mostly-unchanging features in future
releases.

Semantic versioning may not make sense for this kind of project because it's
not a library nor an API so `1.0` and future releases may well use calendar
versioning or something similar.

The [ROADMAP.md](./ROADMAP.md) document has more fine grained details on which
features are planned and what's currently being worked on.


## Licensing

I'm currently undecided on licensing.

**I will not enforce my copyright against anyone merely viewing the code.**

**Because no license has been specified, I maintain full copyright, as with any
other project without a license.**

In the near future, I'll probably choose between OSL-3.0, GPLv3, BSD and MIT.

If you really want/need to use the code in this project in the current very
early-stage state, submit an issue and we'll figure something out.

