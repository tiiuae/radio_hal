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

ifndef CC
CC=gcc
endif

ifndef CXXSTD
CXXSTD=gnu++14
endif

ifndef CSTD
CSTD=gnu11
endif


ifndef CFLAGS
CFLAGS = -MMD -O2 -Wall -g -fPIC -I$(INC_DIR) -I$(SRC_DIR)/$(WIFI_HAL_DIR) -I$(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/ -I/usr/include/libnl3/ -std=${CSTD}
CXXFLAGS = -MMD -O2 -Wall -g -fPIC -I$(INC_DIR) -I$(SRC_DIR)/$(WIFI_HAL_DIR) -I$(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/ -I/usr/include/libnl3/ -std=${CXXSTD}
endif

LDFLAGS=$(shell pkg-config --libs libnl-3.0 libnl-genl-3.0)

OBJ=$(SRC_DIR)/$(CMN_SRC_DIR)/radio_hal_main.o $(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/os_unix.o $(SRC_DIR)/$(WIFI_HAL_DIR)/$(WPA_CTL_DIR)/wpa_ctrl.o $(SRC_DIR)/$(WIFI_HAL_DIR)/wifi_hal_main.o

${ALL}: $(OBJ)

%.o: %.cpp
	$(CXX) -c -fPIC  $(CXXFLAGS) ${COPTS} $< -o $@
	echo " CXX " $<

%.o: %.c
	$(CC) -c -fPIC  $(CFLAGS) ${COPTS} $< -o $@
	echo " CC " $<


libradio_hal.so: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(CXXFLAGS) -shared -fPIC $^


radio_hal_daemon: libradio_hal.so
	$(CXX) -o $@ $(CXXFLAGS) $(CXXFLAGS) -Wall $^ -L. -lradio_hal $(LDFLAGS)

install:
	cp -a -f radio_hal_daemon $(INSTALL_ROOT)/usr/bin/
	cp -a -f libradio_hal.so $(INSTALL_ROOT)/usr/lib/

clean:
	rm -frv *.so $(OBJ) $(OBJ:.o=.d) ${ALL}
