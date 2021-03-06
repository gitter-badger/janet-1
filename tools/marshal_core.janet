# Tool to dump a marshalled version of the janet core to stdout. The
# image should eventually allow janet to be started from a pre-compiled
# image rather than recompiled every time from the embedded source. More
# work will go into shrinking the image (it isn't currently that large but
# could be smaller), creating the mechanism to load the image, and modifying
# the build process to compile janet with a built image rather than
# embedded source.

# Get image. This image contains as much of the core library and documentation that
# can be written to an image (no cfunctions, no abstracts (stdout, stdin, stderr)),
# everything else goes. Cfunctions and abstracts will be referenced from a register
# table which will be generated on janet startup.
(def image (let [env-pairs (pairs (env-lookup *env*))
                 essential-pairs (filter (fn [[k v]] (or (cfunction? v) (abstract? v))) env-pairs)
                 lookup (table ;(mapcat identity essential-pairs))
                 reverse-lookup (invert lookup)]
             (marshal (table/getproto *env*) reverse-lookup)))

# Write image
(file/write stdout image)
(file/flush stdout)
