# libmyc

Just a simple C library

This was a junk drawer of C functionality that wasn't straightforward (in some
way), or wasn't widely available, or was used in more than a few of my
"script"-y C programs.

This is very old code of mine.  (As I write this README, it's been 5 years
since I committed it to source control, which means some of it is probably a
decade old)

- Some of it's kind of fun:
    - Perl-style "die" and "warn" functions (vararg handling)

- Some of it's kind of useful:
    - The array stuff was for my photomosaic tool.
    - lots of `malloc` variants with bail-out on failure
    - `gen_counter` generates a counter with a bunch of options

- Some of it is really useful:
    - `NOW` = current time as a double
    - `starts_with` and `ends_with` (What kind of stdlib omits these?)

- Some of it's horribly dumb:
    - `auto_remake` function shells out to `make` if the source file is updated
        - was useful while developing my photomosaic tool
        - ...because I'd not heard of much better ways of doing that
