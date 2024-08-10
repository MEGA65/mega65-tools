# Changelog

Please look into [mega65-tools](https://github.com/MEGA65/mega65-tools/),
for a complete changelog.

## Release 1.00

This is our first official release after switching from the *master/development*
branch setup to the new *development-is-main, release-in branch* workflow.

The change was made to make it more clear that a branch with continous potentially
unchecked commits can't be a real release, which (at least in our eyes) implies
some sort of quality control.

### Changes

A lot! As this is the first official release in this pattern, we obviously won't
list everything from the start. Look at the
[R1.00 commit history](https://github.com/MEGA65/mega65-tools/commits/release-1.00/)
to find out more!

Some noteable things:

- `coretool` is the new official swiss army knife for core files. Both `bit2core`
  and `bit2mcs` are replaced by it.
- `etherload` and `mega65_ftp` allow you to use an IPv6 link to your mega65 to
  transfer files to it and run them.
