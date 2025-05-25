# Compilador e flags
CC = gcc

# Arquivos objeto pré-compilados
STATIC_OBJS = ppos-all.o queue.o

preempcao:
	$(CC) -o ppos-teste ppos-core-aux.c pingpong-scheduler.c $(STATIC_OBJS)

# Limpeza
clean:
	rm -f ppos-teste
