# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>

INSTALL(FILES 
PAL/bi-linear_x.pgm
PAL/bi-linear_y.pgm
PAL/burst.pgm
PAL/checkerboard_small.pgm
PAL/clock.pgm
PAL/cloud.pgm
PAL/curtain.pgm
PAL/horizontal_blinds.pgm
PAL/linear_x.pgm
PAL/linear_y.pgm
PAL/radial-bars.pgm
PAL/radial.pgm
PAL/spiral.pgm
PAL/spiral2.pgm
PAL/square.pgm
PAL/square2-bars.pgm
PAL/square2.pgm
PAL/symmetric_clock.pgm
DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/lumas/PAL)

INSTALL(FILES 
HD/bi-linear_x.pgm
HD/bi-linear_y.pgm
HD/burst.pgm
HD/checkerboard_small.pgm
HD/clock.pgm
HD/cloud.pgm
HD/curtain.pgm
HD/horizontal_blinds.pgm
HD/linear_x.pgm
HD/linear_y.pgm
HD/radial-bars.pgm
HD/radial.pgm
HD/spiral.pgm
HD/spiral2.pgm
HD/square.pgm
HD/square2-bars.pgm
HD/square2.pgm
HD/symmetric_clock.pgm
DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/lumas/HD)
