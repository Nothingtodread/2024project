# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Alberto Villa <avilla@FreeBSD.org>, Laurent Montel <montel@kde.org>, Jean-Baptiste Mardelle <jb@kdenlive.org>

set(sampleplugin_SRCS sampleplugin.cpp)

ki18n_wrap_ui(sampleplugin_UIS countdown_ui.ui)

add_library(kdenlive_sampleplugin MODULE WITH_PREFIX
  ${sampleplugin_SRCS}
  ${sampleplugin_UIS}
)

add_definitions(${KDE4_DEFINITIONS})


include(${QT_USE_FILE})

#add_definitions(${QT_DEFINITIONS})
#add_definitions(-DQT_NO_DEBUG)
add_definitions(-DQT_PLUGIN)
add_definitions(-DQT_SHARED)

target_link_libraries(kdenlive_sampleplugin
  KF5::KDELibs4Support
  
  KF5::KIOCore
  ${QT_LIBRARIES}
)

install(TARGETS kdenlive_sampleplugin DESTINATION ${KDE_INSTALL_PLUGINDIR})
