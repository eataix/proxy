#===============================================================================
#
#     Filename: Makefile
#  Description:
#
#        Usage: make              (generate executable                      )
#               make clean        (remove objects, executable, prerequisits )
#               make tarball      (generate compressed archive              )
#               make zip          (generate compressed archive              )
#
#      Version: 1.0
#      Created:
#     Revision: ---
#
#       Author: Meitian Huang
#        Email:
#
#        Notes: This is a GNU make (gmake) makefile.
#               C   extension   :  c
#               Prerequisites are generated automatically; makedepend is not
#               needed (see documentation for GNU make Version 3.81, April 2006,
#               section 4.13). The utility sed is used.
#========================================== makefile template version 1.9 ======

# OPENSSL can be set to YES to enable the experimental support for OpenSSL, or
# NO otherwise

OPENSSL        := NO

# DEBUG can be set to YES to include debugging info, or NO otherwise
DEBUG          := YES

# PROFILE can be set to YES to include profiling info, or NO otherwise
PROFILE        := NO

# ------------  name of the executable  ----------------------------------------
EXECUTABLE      := webproxy

# ------------  list of all source files  --------------------------------------
SOURCES         := webproxy.c, config.c, readline.c, utils.c

# ------------  list of source files associated with OpenSSL support -----------
OPENSSL_SOURCES := server.c, common.c

# ------------  compiler  ------------------------------------------------------
CC              := clang # I highly recommend clang

# ------------  compiler flags  ------------------------------------------------
DEBUG_CFLAGS    := -Wall -std=gnu99 -g -Wstrict-prototypes -Werror -D __DEBUG__ -Wextra
RELEASE_CFLAGS  := -Wall -std=gnu99 -O3
OPENSSL_CFLAGS  := -D __OPENSSL_SUPPORT__

# ------------  linker flags  --------------------------------------------------
DEBUG_LDFLAGS    :=
RELEASE_LDFLAGS  :=
OPENSSL_LDFLAGS  := -lssl -lcrypto

ifeq (YES, ${DEBUG})
  CFLAGS       := ${DEBUG_CFLAGS}
  LDFLAGS      := ${DEBUG_LDFLAGS}
else
  CFLAGS       := ${RELEASE_CFLAGS}
  LDFLAGS      := ${RELEASE_LDFLAGS}
endif

ifeq (YES, ${PROFILE})
  CFLAGS       := ${CFLAGS} -pg -O3
  LDFLAGS      := ${LDFLAGS} -pg
endif

ifeq (YES, ${OPENSSL})
  SOURCES      := ${SOURCES} ${OPENSSL_SOURCES}
  CFLAGS       := ${CFLAGS} ${OPENSSL_CFLAGS}
  LDFLAGS      := ${LDFLAGS} ${OPENSSL_LDFLAGS}
endif

# ------------  additional system include directories  -------------------------
GLOBAL_INC_DIR  = /usr/include

# ------------  private include directories  -----------------------------------
LOCAL_INC_DIR   =

# ------------  system libraries  (e.g. -lm )  ---------------------------------
SYS_LIBS        = -lrt -pthread

# ------------  additional system library directories  -------------------------
GLOBAL_LIB_DIR  = /usr/lib

# ------------  additional system libraries  -----------------------------------
GLOBAL_LIBS     =

# ------------  private library directories  -----------------------------------
LOCAL_LIB_DIR   =

# ------------  private libraries  (e.g. libxyz.a )  ---------------------------
LOCAL_LIBS      =

# ------------  archive generation ---------------------------------------------
TARBALL_EXCLUDE = *.{o,gz,zip}
ZIP_EXCLUDE     = *.{o,gz,zip}

# ------------  run executable out of this Makefile  (yes/no)  -----------------
# ------------  cmd line parameters for this executable  -----------------------
EXE_START       = no
EXE_CMDLINE     =

#===============================================================================
# The following statements usually need not to be changed
#===============================================================================

C_SOURCES       = $(filter     %.c, $(SOURCES))
ALL_INC_DIR     = $(addprefix -I, $(LOCAL_INC_DIR) $(GLOBAL_INC_DIR))
ALL_LIB_DIR     = $(addprefix -L, $(LOCAL_LIB_DIR) $(GLOBAL_LIB_DIR))
GLOBAL_LIBSS    = $(addprefix $(GLOBAL_LIB_DIR)/, $(GLOBAL_LIBS))
LOCAL_LIBSS     = $(addprefix $(LOCAL_LIB_DIR)/, $(LOCAL_LIBS))
ALL_CFLAGS      = $(CFLAGS) $(ALL_INC_DIR)
ALL_LFLAGS      = $(LDFLAGS) $(ALL_LIB_DIR)
BASENAMES       = $(basename $(SOURCES))

# ------------  generate the names of the object files  ------------------------
OBJECTS         = $(addsuffix .o,$(BASENAMES))

# ------------  generate the names of the hidden prerequisite files  -----------
PREREQUISITES   = $(addprefix .,$(addsuffix .d,$(BASENAMES)))

# ------------  make the executable (the default goal)  ------------------------
$(EXECUTABLE):	$(OBJECTS)
	$(CC)  $(ALL_LFLAGS) -o $(EXECUTABLE) $(OBJECTS) $(LOCAL_LIBSS) $(GLOBAL_LIBSS) $(SYS_LIBS)
ifeq ($(EXE_START),yes)
								./$(EXECUTABLE) $(EXE_CMDLINE)
endif

# ------------  include the automatically generated prerequisites  -------------
# ------------  if target is not clean, tarball or zip             -------------
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),tarball)
ifneq ($(MAKECMDGOALS),zip)
-include         $(PREREQUISITES)
endif
endif
endif

# ------------  make the objects  ----------------------------------------------
%.o:		%.c
				$(CC)  -c $(ALL_CFLAGS) $<

# ------------  make the prerequisites  ----------------------------------------
#
.%.d:   %.c
				@$(make-prerequisite-c)

define	make-prerequisite-c
				@$(CC)   -MM $(ALL_CFLAGS) $< > $@.$$$$;            \
				sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' < $@.$$$$ > $@; \
				rm -f $@.$$$$;
endef

# ------------  remove generated files  ----------------------------------------
# ------------  remove hidden backup files  ------------------------------------
clean:
	-rm  -f $(EXECUTABLE) $(OBJECTS) $(PREREQUISITES) *~

# ------------ tarball generation ----------------------------------------------
tarball:
	@lokaldir=`pwd`; lokaldir=$${lokaldir##*/}; \
					rm --force $$lokaldir.tar.gz;               \
					tar --exclude=$(TARBALL_EXCLUDE)            \
					--create                                \
					--gzip                                  \
					--verbose                               \
					--file  $$lokaldir.tar.gz *

# ------------ zip -------------------------------------------------------------
zip:
					@lokaldir=`pwd`; lokaldir=$${lokaldir##*/}; \
					zip -r  $$lokaldir.zip * -x $(ZIP_EXCLUDE)

tags:
	rm -f tags
	ctags -R .

.PHONY: clean tarball zip tags

# ==============================================================================
# vim: set tabstop=2: set shiftwidth=2:
