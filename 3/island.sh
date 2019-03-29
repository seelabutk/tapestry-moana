source runvars

CAMERA="-sg:apertureRadius=0.003125 -sg:focusDistance=1675.338285 -sg:fovy=32.4138"
CAMERA="${CAMERA} -vp -1139.015893 23.286733 1479.794723    -vi  244.814337 238.807148 560.380117    -vu -0 1 0"

${OSP}  ${COMMON} ${BIFFS}/island.biff ${CAMERA} $@
