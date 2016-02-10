OUT        = compiler
SRC        = ${wildcard *.cpp}
OBJ        = ${SRC:.cpp=.o}
DEPENDS    = .depends

CXXFLAGS  += -g -O2 -Wall -std=c++11
LDFLAGS   += 

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
