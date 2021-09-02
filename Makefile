ALL=radio_hal_daemon
# install

all: ${ALL}

LDFLAGS += -rdynamic
INC_DIR = inc
SRC_DIR = src
CMN_SRC_DIR = common
WIFI_HAL_DIR = wifi
WPA_CTL_DIR = wpa_socket

ifndef CXX
CXX=g++
endif

ifndef CXXSTD
CXXSTD=c++14
endif

ifndef CFLAGS
CFLAGS = -MMD -O2 -Wall -g -fPIC -I$(INC_DIR) -I$(SRC_DIR)/$(WIFI_HAL_DIR) -I$(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/ -I/usr/include/libnl3/
CFLAGS += -std=${CXXSTD}
endif

LDFLAGS=$(shell pkg-config --libs libnl-3.0 libnl-genl-3.0)

OBJ=$(SRC_DIR)/$(CMN_SRC_DIR)/radio_hal_main.o $(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/os_unix.o $(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/wpa_ctrl.o $(SRC_DIR)/$(WIFI_HAL_DIR)/wifi_hal_main.o

%.o: %.cpp
	$(CXX) -c -fPIC  $(CFLAGS) ${COPTS} $< -o $@
	echo " CXX " $<

libradio_hal.so: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(CFLAGS) -shared -fPIC $^


radio_hal_daemon: libradio_hal.so
	$(CXX) -o $@ $(CXXFLAGS) $(CFLAGS) -Wall $^ -L. -lradio_hal $(LDFLAGS)

install:
	cp -a -f radio_hal_daemon $(INSTALL_ROOT)/usr/bin/
	cp -a -f libradio_hal.so $(INSTALL_ROOT)/usr/lib/

clean:
	rm -f *.so *.o *.d ${ALL}
