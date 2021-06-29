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
LIB_SRCS := iftracer_hook.cpp mmap_writer.cpp
LIB_OBJ  := iftracer_hook.o mmap_writer.o

MMAP_WRITER_TEST := mmap_writer_test
MMAP_WRITER_TEST_SRCS := mmap_writer_test.cpp
MMAP_WRITER_TEST_OBJ  := mmap_writer_test.o

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
	$(CXX) $^ $(CXXFLAGS) -c -ggdb3 -o $(APP_OBJ) $(INSTRUMENT_FLAGS)

$(MMAP_WRITER_TEST): $(MMAP_WRITER_TEST_OBJ) $(LIB_OBJ)
	$(CXX) $^ $(CXXFLAGS) -ggdb3 -o $(MMAP_WRITER_TEST)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c -ggdb3 $<

.PHONY: clean
clean:
	$(RM) $(APP) $(APP_OBJ) $(LIB_OBJ) $(MMAP_WRITER_TEST) $(MMAP_WRITER_TEST_OBJ) depend.inc
	$(RM) ./iftracer.out.* mmap_writer_test.bin

.PHONY: clean.out
clean.out:
	$(RM) ./iftracer.out.* mmap_writer_test.bin

# .PHONY: depend
# depend:
	# makedepend -- $(CXXFLAGS) -- $(ALL_C_FILES)

.PHONY: run
run: $(APP)
	echo "[RUN EXAMPLE]"
	./$(APP)

.PHONY: test
test: $(MMAP_WRITER_TEST)
	echo "[RUN TEST]"
	./$(MMAP_WRITER_TEST)

# -include depend.inc
# # DO NOT DELETE
