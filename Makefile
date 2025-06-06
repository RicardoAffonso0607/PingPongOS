# Compilador e flags
CC = gcc

# Arquivos objeto pré-compilados
STATIC_OBJS = ppos-all.o queue.o

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
