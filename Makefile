OUT        = compiler
SRC        = ${wildcard *.cpp}
OBJ        = ${SRC:.cpp=.o}
DEPENDS    = .depends

# Note: we use -fexceptions because otherwise Boost complains that
# boost::throw_exception can't be resolved
CXXFLAGS  += -g -O2 -Wall -std=c++11 $(shell llvm-config --cxxflags) -fexceptions
LDFLAGS   += $(shell llvm-config --ldflags --system-libs --libs core)

all: ${OUT}

${OUT}: ${OBJ}
	${CXX} -o $@ ${OBJ} ${LDFLAGS}

.cpp.o:
	${CXX} -c -o $@ $< ${CXXFLAGS}

${DEPENDS}: ${SRC}
	${RM} -f ./${DEPENDS}
	${CXX} ${CXXFLAGS} -MM $^ >> ./${DEPENDS}

clean:
	${RM} ${OUT} ${OBJ} ${DEPENDS}

-include ${DEPENDS}
.PHONY: all clean
