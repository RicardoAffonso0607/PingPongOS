# Compilador e flags
CC = gcc

# Arquivos objeto pré-compilados
STATIC_OBJS = ppos-core-aux.c ppos-all.o queue.o disk-driver.o

.PHONY: disco1 disco2

disco1: pingpong-disco1.c
	@cp disk_original.dat disk.dat
	gcc -Wall -lrt -o $@ $< $(STATIC_OBJS)
	./$@ > operacoes_disco.txt
	rm -f $@	

disco2: pingpong-disco2.c
	@cp disk_original.dat disk.dat
	gcc -Wall -lrt -o $@ $< $(STATIC_OBJS)
	./$@ > operacoes_disco.txt
	rm -f $@

scheduler:
	$(CC) -o ppos-teste ppos-core-aux.c pingpong-scheduler.c $(STATIC_OBJS)

preempcao:
	$(CC) -o ppos-teste ppos-core-aux.c pingpong-preempcao.c $(STATIC_OBJS)

preempcao-stress:
	$(CC) -o ppos-teste ppos-core-aux.c pingpong-preempcao-stress.c $(STATIC_OBJS)

contab:
	$(CC) -o ppos-teste ppos-core-aux.c pingpong-contab-prio.c $(STATIC_OBJS)

# Limpeza
clean:
	rm -f ppos-teste
