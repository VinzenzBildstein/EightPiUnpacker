.EXPORT_ALL_VARIABLES:

.SUFFIXES:

.PHONY: clean all

# := is only evaluated once

SHELL 		= /bin/sh

NAME		= EightPiUnpacker

LIB_DIR 	= $(HOME)/lib
BIN_DIR		= $(HOME)/bin

ROOTLIBS     	:= $(shell root-config --libs)
ROOTINC      	:= -I$(shell root-config --incdir)

COMMON_DIR 	= $(HOME)/Common

INCLUDES        = -I$(COMMON_DIR) -I.

LIBRARIES	= CommandLineInterface Utilities TextAttributes Spectrum pthread boost_iostreams pugixml XMLParser

CC		= gcc
CXX             = g++
CPPFLAGS 	= $(ROOTINC) $(INCLUDES) -fPIC
CXXFLAGS	= -std=c++11 -pedantic -Wall -Wno-long-long -g -O3

LDFLAGS		= -g -fPIC

LDLIBS 		= -L$(LIB_DIR) -Wl,-rpath,/opt/gcc/lib64 $(ROOTLIBS) $(addprefix -l,$(LIBRARIES))

LOADLIBES = \
	MidasFileManager.o \
	MidasEventProcessor.o \
	Event.o \
	Settings.o \
	Odb.o \
	$(NAME)Dictionary.o

# -------------------- implicit rules --------------------
# n.o made from n.c by 		$(CC) -c $(CPPFLAGS) $(CFLAGS)
# n.o made from n.cc by 	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS)
# n made from n.o by 		$(CC) $(LDFLAGS) n.o $(LOADLIBES) $(LDLIBS)

# -------------------- rules --------------------

all:  $(NAME) lib$(NAME).so
	@echo Done

# -------------------- libraries --------------------

lib$(NAME).so: $(LOADLIBES)
	$(CXX) $(LDFLAGS) -shared -Wl,-soname,lib$(NAME).so -o lib$(NAME).so.1.0.1 $(LOADLIBES) -lc

$(LIB_DIR)/libCommandLineInterface.so:
	@cd $(COMMON_DIR); make $@

$(LIB_DIR)/libMacros.so:
	@cd $(MACRO_DIR); make $@

# -------------------- pattern rules --------------------
# this rule sets the name of the .cc file at the beginning of the line (easier to find)

%.o: %.cc %.hh
	$(CXX) $< -c $(CPPFLAGS) $(CXXFLAGS) -o $@

# -------------------- default rule for executables --------------------

%: %.cc $(LOADLIBES)
	$(CXX) $< $(CXXFLAGS) $(CPPFLAGS) $(LOADLIBES) $(LDLIBS) -o $@

# -------------------- Root stuff --------------------

DEPENDENCIES = \
	Event.hh \
	RootLinkDef.h

$(NAME)Dictionary.o: $(NAME)Dictionary.cc $(NAME)Dictionary.h
	 $(CXX) -fPIC $(CXXFLAGS) $(CPPFLAGS) -c $<

$(NAME)Dictionary.cc: $(DEPENDENCIES)
	 rm -f $(NAME)Dictionary.cc $(NAME)Dictionary.h; rootcint -f $@ -c $(CPPFLAGS) $(DEPENDENCIES)

# -------------------- tar ball --------------------

tar:
	@echo "creating zipped tar-ball ... "
	tar -chvzf $(NAME).tar.gz Makefile \
	*.hh *.cc \
	Settings.dat

# -------------------- clean --------------------

clean:
	rm  -f $(NAME) *.o
