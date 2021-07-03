CC := $(if $(CC),$(CC),gcc)
CXX := $(if $(CXX),$(CXX),g++)
AR := $(if $(AR),$(AR),ar)
STRIP := $(if $(STRIP),$(STRIP),strip)
RANLIB := $(if $(RANLIB),$(RANLIB),ranlib)
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

ALL_SRCS=$(APP_SRCS) $(LIB_SRCS) $(MMAP_WRITER_TEST_SRCS)
DEPENDS=$(ALL_SRCS:%.cpp=%.d)
DEPENDS_FLAGS=-MMD -MP

APP_FLAGS := -DIFTRACER_ENABLE_API -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++
ifneq ($(filter clang%,$(CXX)),)
	APP_FLAGS := -DIFTRACER_ENABLE_API -finstrument-functions-after-inlining
endif

.SUFFIXES: .cpp .c .o

.PHONY: all
all: $(APP)

$(APP): $(APP_OBJ) $(LIB_OBJ)
	$(CXX) $^ $(CXXFLAGS) -lpthread -g1 -o $(APP)

$(APP_OBJ): $(APP_SRCS)
	$(CXX) $^ $(CXXFLAGS) $(DEPENDS_FLAGS) -c -g1 -o $(APP_OBJ) $(APP_FLAGS)

$(MMAP_WRITER_TEST): $(MMAP_WRITER_TEST_OBJ) $(LIB_OBJ)
	$(CXX) $^ $(CXXFLAGS) -g3 -o $(MMAP_WRITER_TEST)

.cpp.o:
	$(CXX) $(CXXFLAGS) $(DEPENDS_FLAGS) -c -g3 $<

.PHONY: clean
clean:
	$(RM) $(APP) $(APP_OBJ) $(LIB_OBJ) $(MMAP_WRITER_TEST) $(MMAP_WRITER_TEST_OBJ) $(DEPENDS)
	$(RM) ./iftracer.out.* mmap_writer_test.bin

.PHONY: clean.out
clean.out:
	$(RM) ./iftracer.out.* mmap_writer_test.bin

.PHONY: run
run: $(APP)
	@echo "[RUN EXAMPLE]"
	./$(APP)

.PHONY: test
test: $(MMAP_WRITER_TEST)
	@echo "[RUN TEST]"
	./$(MMAP_WRITER_TEST)

-include $(DEPENDS)
