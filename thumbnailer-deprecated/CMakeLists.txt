# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>

set(mltpreview_SRCS mltpreview.cpp ../src/lib/localeHandling.cpp)

include_directories(
  ${MLT_INCLUDE_DIR}
  ${MLTPP_INCLUDE_DIR}
)

add_library(mltpreview MODULE ${mltpreview_SRCS})


target_link_libraries(mltpreview
        Qt${QT_MAJOR_VERSION}::Core
        Qt${QT_MAJOR_VERSION}::Gui
        KF5::KIOCore
        KF5::KIOWidgets
  ${MLT_LIBRARIES}
  ${MLTPP_LIBRARIES}
)
#FIXME: For some reason the plugin only seems to work if installed in $lib/qt5/plugins and not in $lib/plugins like KF5 recommends
install(TARGETS mltpreview DESTINATION ${KDE_INSTALL_PLUGINDIR})
install(FILES mltpreview.desktop DESTINATION ${KDE_INSTALL_KSERVICESDIR})
