CC := gcc
CXX := g++
CFLAGS := -std=c++11 -Wall -O3
CXXFLAGS := -std=c++11 -Wall -O3

ifeq ($(shell uname -s),Darwin)
	CXX := clang++
endif

APP := iftracer_main
APP_SRCS := main.cpp
APP_OBJ  := main.o
LIB_SRCS := iftracer_hook.cpp
LIB_OBJ  := iftracer_hook.o

INSTRUMENT_FLAGS := -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++
ifneq ($(filter clang%,$(CXX)),)
	INSTRUMENT_FLAGS := -finstrument-functions-after-inlining
endif

.SUFFIXES: .cpp .c .o

.PHONY: all
all: $(APP)
# all: depend $(APP)

$(APP): $(APP_OBJ) $(LIB_OBJ)
	$(CXX) $^ $(CXXFLAGS) -lpthread -ggdb3 -o $(APP)

$(APP_OBJ): $(APP_SRCS)
	$(CXX) $^ $(CXXFLAGS) -c -lpthread -ggdb3 -o $(APP_OBJ) $(INSTRUMENT_FLAGS)

$(LIB_OBJ): $(LIB_SRCS)
	$(CXX) $^ $(CXXFLAGS) -c -lpthread -ggdb3 -o $(LIB_OBJ)

.c.o:
	$(CXX) $(CXXFLAGS) -c $<

.PHONY: clean
clean:
	$(RM) $(APP) $(APP_OBJ) $(LIB_OBJ) depend.inc
	rm -rf ./iftracer.out.*

# .PHONY: depend
# depend:
	# makedepend -- $(CXXFLAGS) -- $(ALL_C_FILES)

.PHONY: test
test: $(APP)
	echo "[TEST]"
	./$(APP)

# -include depend.inc
# # DO NOT DELETE
