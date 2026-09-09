# Recover the total mooring wrench time series from a coupled solver log.
#
# moorDynR2 prints, once per CFD time step:
#   t = <T> X[6dof]: (x y z), (rx ry rz)
#    body <name> force (Fx Fy Fz) moment (Mx My Mz)
# This pairs the two lines into one row. The wrench is the TOTAL mooring
# reaction on the body (all lines combined) — per-line fairlead tensions are
# only in MoorDyn's lines_oc4.out and cannot be recovered from the log.
#
# Usage:  awk -f extract_mooring_wrench_from_log.awk slurm-<job>.out > out.dat
# Output: time x y z rx ry rz Fx Fy Fz Mx My Mz   (space-separated, header #)
BEGIN { print "# t x y z rx ry rz Fx Fy Fz Mx My Mz  (total mooring wrench from moorDynR2 log lines)" }
{ gsub(/\r/, "") }   # MoorDyn emits \r-prefixed progress lines
/^t = [0-9.eE+-]+ X\[6dof\]:/ {
    line = $0; gsub(/[(),]/, " ", line); n = split(line, a, /[ \t]+/)
    # a: t = T X[6dof]: x y z rx ry rz
    t = a[3]; for (i = 1; i <= 6; i++) pos[i] = a[4 + i]
    havePos = 1; next
}
/^ *body .* force .* moment / {
    if (!havePos) next
    line = $0; gsub(/[(),]/, " ", line); n = split(line, a, /[ \t]+/)
    # a (after leading-space split may yield empty a[1]): find "force"/"moment"
    fi = 0; mi = 0
    for (i = 1; i <= n; i++) { if (a[i] == "force") fi = i; if (a[i] == "moment") mi = i }
    if (!fi || !mi) next
    printf "%s %s %s %s %s %s %s %s %s %s %s %s %s\n", \
        t, pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], \
        a[fi+1], a[fi+2], a[fi+3], a[mi+1], a[mi+2], a[mi+3]
    havePos = 0
}
