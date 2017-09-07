## A converter from FBX to p3db
```
Usage: C:\projects\pb-fbx-conv\pb-fbx-conv.exe [options] filename [outfile]
Options:
  -m maxVerts   limit the max number of vertices in a [m]esh (default 32768)
  -b maxBones   limit the max number of [b]ones in a draw call (default 12)
  -w maxWeights limit the max number of bone [w]eights per vertex (default 4)
  -f            [f]lip the V texture axis
  -p            [p]ack vertex colors into 4 bytes
  -j            output g3d[j] instead of g3db
  -r samplerate frame [r]ate at which to sample animations
  -s playspeed  animation playback [s]peed, will be used to scale the sample rate
  -a            output p3db [a]nimations instead of g3db
  -h or -?      display this [h]elp message and exit
  -v            legacy flag, its [v]alue is ignored.
  -o ignored    legacy flag, its value is ign[o]red.

Debugging Options:
  -d [flags]    dump intermediate data. Possible flags are:
                  o  dump the fbx [o]bject tree to the file 'objects.out'
                  e  dump the fbx [e]lement tree to the file 'elements.out'
                  n  dump the fbx [n]ode tree to the file 'nodes.out'
                  m  dump the fbx [m]aterials to the console
                  M  dump the fbx [M]eshes to the console
                  g  dump the fbx [g]eometry to the console
                  O  dump a .obj file containing the tesselated geometry to 'geom.obj'
```
