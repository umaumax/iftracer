CC := $(if $(CC),$(CC),gcc)
CXX := $(if $(CXX),$(CXX),g++)
AR := $(if $(AR),$(AR),ar)
STRIP := $(if $(STRIP),$(STRIP),strip)
RANLIB := $(if $(RANLIB),$(RANLIB),ranlib)
CFLAGS := -std=c++11 -Wall -O3
CXXFLAGS := -std=c++11 -Wall -O3

ifeq ($(IFTRACER_LOCK_FREE_QUEUE), 1)
	CXXFLAGS += -DIFTRACER_LOCK_FREE_QUEUE
endif

ifeq ($(shell uname -s),Darwin)
	CXX := clang++
endif

APP := iftracer_main
APP_SRCS := main.cpp
APP_OBJ  := main.o
LIB_SRCS := iftracer_hook.cpp mmap_writer.cpp
LIB_OBJ  := mmap_writer.o iftracer_hook.o

MMAP_WRITER_TEST := mmap_writer_test
MMAP_WRITER_TEST_SRCS := mmap_writer_test.cpp
MMAP_WRITER_TEST_OBJ  := mmap_writer_test.o

LIB_AR=libiftracer.a
ARFLAGS=crvs

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

$(APP): $(APP_OBJ) $(LIB_AR)
	$(CXX) $(CXXFLAGS) -g1 -o $(APP) $^ -lpthread 

$(APP_OBJ): $(APP_SRCS)
	$(CXX) $< $(CXXFLAGS) $(DEPENDS_FLAGS) -c -g1 -o $(APP_OBJ) $(APP_FLAGS)

$(MMAP_WRITER_TEST): $(MMAP_WRITER_TEST_OBJ) $(LIB_OBJ)
	$(CXX) $^ $(CXXFLAGS) -g3 -o $(MMAP_WRITER_TEST)

$(LIB_AR): $(LIB_OBJ)
	$(AR) $(ARFLAGS) $@ $^

.cpp.o:
	$(CXX) $(CXXFLAGS) $(DEPENDS_FLAGS) -c -g3 $<

.PHONY: clean
clean:
	$(RM) $(APP) $(APP_OBJ) $(LIB_OBJ) $(MMAP_WRITER_TEST) $(MMAP_WRITER_TEST_OBJ) $(LIB_AR) $(DEPENDS)
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
