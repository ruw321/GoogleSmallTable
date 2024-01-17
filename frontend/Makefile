TARGETS = sev loadBalancer

all: $(TARGETS)

sev: sev.cc
	g++ $^ -lpthread -g -o $@

loadBalancer: loadBalancer.cc
	g++ $^ -lpthread -g -o $@

clean::
	rm -fv $(TARGETS) *~

