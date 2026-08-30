# peranti
A luanti renderer rewrite written using sokol in c that is highly performant (also malay word for device)
## Warning: no windows support yet! use at your own risk!
also make sure your luanti settings are opengl, not ES and that you have opengl 3.3+ as your version
### build how? build.sh gives an error/running build/peranti gives me errors!
Maybe run this: `git submodule update --init --recursive` then `./build-zig.sh` and check build directory for new executables.
Also make sure you have luanti source code.
To install this renderer patch, you must have luanti's source code, and put the replacement files in luanti/src/client alongside the src folder of this project as peranti. Also, move third_party into the peranti folder too.
### how i contribute?
Implement an item on the status list in a way that is good enough (or better than my current implementation)
### Status (features)
none
