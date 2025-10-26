PROJECT_NAME = casproxyserver

SRC_DIR = src
OBJ_DIR = build

SRC_FILES = $(wildcard $(SRC_DIR)/*.cpp)

OBJ_FILES = $(SRC_FILES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

PCSC_INC = $(shell pkg-config --cflags-only-I libpcsclite)
PCSC_LIB = $(shell pkg-config --libs libpcsclite)

YAML_CPP_INC = $(shell pkg-config --cflags-only-I yaml-cpp)
YAML_CPP_LIB = $(shell pkg-config --libs yaml-cpp)

CXX = g++
CXXFLAGS = -std=c++17 -Wall $(PCSC_INC) $(YAML_CPP_INC) -Ithirdparty/asio/asio/include -Ithirdparty/rapidjson/include -Ithirdparty/websocketpp
LDFLAGS = $(PCSC_LIB) $(YAML_CPP_LIB)

EXEC = $(OBJ_DIR)/$(PROJECT_NAME)

all: $(EXEC)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(EXEC): $(OBJ_FILES)
	$(CXX) $(OBJ_FILES) $(LDFLAGS) -o $(EXEC)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

install:
	cp $(EXEC) /usr/local/bin/$(PROJECT_NAME)

.PHONY: all clean install