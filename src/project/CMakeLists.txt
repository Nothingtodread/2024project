# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Vincent PINON <vincent.pinon@laposte.net>

add_subdirectory(dialogs)
set(kdenlive_SRCS
  ${kdenlive_SRCS}
  project/clipstabilize.cpp
  project/cliptranscode.cpp
  project/invaliddialog.cpp
  #project/projectcommands.cpp
  project/projectmanager.cpp
  project/effectsettings.cpp
  project/notesplugin.cpp
  project/transcodeseek.cpp
  PARENT_SCOPE)
