Station pins are installed at TheJockeyClub-courses/<station>.pins.txt.

`segment <note>` retains the existing optional recorded ride semantics.
`centerline <note>` identifies a required stretch of the recorded centre by
world anchors. Projection failure or incompatible physical evidence refuses
the line; it cannot silently retry an offset route. The solver fixes both ends
of every intersecting chord to zero, including entrance and exit boundaries.
Approaches participate in the same solve. Utilization, terrain preference,
and replanning cannot move those fixed nodes.

Arthur's source file preserves the existing Saint Denis and Clemens Point
stretches byte for byte, then adds the two narrow bridges as one required
stretch covering the original recorded centre from5538 through5640 metres.
The103 world anchors were interpolated from the existing recorded geometry
7f2a5d38dd93eaff; runtime projects them, so no progress-distance switch exists.
