# peranti
a luanti compatible (planned for next month) voxel game engine written using vulkan in c that is highly performant (also malay word for device)
![my game be like](./ex.gif)
## Warning: no windows support yet! use at your own risk!
### build how? build.sh gives an error/running build/peranti gives me errors!
maybe run this: `git submodule update --init --recursive` then `./build.sh` and check build directory for new executables.
make sure if you're on linux that you have x11 and wayland development libs on your system, as well as vulkan validation layers if you want to contribute/debug (if you don't, just uncomment `#define NDEBUG` at the top of src/main.c)
### how i contribute?
Implement an item on the status list in a way that is good enough (or better than my current implementation)
### Status (features)
| Feature | Is here? (x for done, <br> - for develompent <br> underway/soon, blank <br> for later) |
| :------- | :---------------: |
| World Loading | [ ] |
| Mods | [ ] |
| Multiplayer | [ ] |
| Chat | [ ] |
| Textures | [ ] |
| Rendering | [ ] |
| Frustum Culling | [] |
| Greedy Meshing | [ ] |
| Face Culling | [ ] |

I just restarted this project, my code discipline wasn't enough, so nothing's here anymore
