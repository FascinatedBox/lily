# - Try to find pq
# Once done this will define
#
#  PQ_FOUND - system has pq
#  PQ_INCLUDE_DIRS - the pq include directory
#  PQ_LIBRARIES - Link these to use pq
#  PQ_DEFINITIONS - Compiler switches required for using pq
#
#  Copyright (c) 2010 Bharanee Rathna <deepfryed@gmail.com>
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


if (PQ_LIBRARIES AND PQ_INCLUDE_DIRS)
  # in cache already
  set(PQ_FOUND TRUE)
else (PQ_LIBRARIES AND PQ_INCLUDE_DIRS)
  find_path(PQ_INCLUDE_DIR
    NAMES
      libpq-fe.h
    PATHS
      /usr/include/postgresql
      /usr/local/include/postgresql
      /opt/local/include
      /opt/local/include/postgresql90
      /opt/local/include/postgresql85
      /opt/local/include/postgresql84
      /sw/include
  )

  find_library(PQ_LIBRARY
    NAMES
      pq
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /opt/local/lib/postgresql90
      /opt/local/lib/postgresql85
      /opt/local/lib/postgresql84
      /sw/lib
  )

  set(PQ_INCLUDE_DIRS
    ${PQ_INCLUDE_DIR}
  )
  set(PQ_LIBRARIES
    ${PQ_LIBRARY}
)

  if (PQ_INCLUDE_DIRS AND PQ_LIBRARIES)
     set(PQ_FOUND TRUE)
  endif (PQ_INCLUDE_DIRS AND PQ_LIBRARIES)

  if (NOT PQ_FOUND)
      message(FATAL_ERROR "Could not find pq")
  endif (NOT PQ_FOUND)

  # show the PQ_INCLUDE_DIRS and PQ_LIBRARIES variables only in the advanced view
  mark_as_advanced(PQ_INCLUDE_DIRS PQ_LIBRARIES)

endif (PQ_LIBRARIES AND PQ_INCLUDE_DIRS)

