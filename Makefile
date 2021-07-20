ALL=radio_hal_daemon
# install

all: ${ALL}

LDFLAGS += -rdynamic
INC_DIR = inc
SRC_DIR = src

ifndef CXX
CXX=g++
endif

ifndef CXXSTD
CXXSTD=c++14
endif

ifndef CFLAGS
CFLAGS = -MMD -O2 -Wall -g -I$(INC_DIR)
CFLAGS += -std=${CXXSTD}
endif

OBJ=$(SRC_DIR)/radio_hal_main.o

%.o: %.cpp
	$(CXX) -c -fPIC $(CFLAGS) ${COPTS} $< -o $@
	echo " CXX " $<

libradio_hal.so: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(CFLAGS) -shared -fPIC $^


radio_hal_daemon: libradio_hal.so
	$(CXX) -o $@ $(CXXFLAGS) $(CFLAGS) -Wall $^ -L. -lradio_hal

install:
	cp -a -f radio_hal_daemon $(INSTALL_ROOT)/usr/bin/

clean:
	rm -f *.so *.o *.d ${ALL}
