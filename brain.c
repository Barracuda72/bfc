#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#define __P_VERSION__ "0.05c"

/*
	Применяем очень хитрую схему организации циклов:

	Как только мы натыкаемся на открывающую скобку, 
	ищем первый свободный номер для цикла.

	Как только натыкаемся на закрывающую - 
	ищем номер последнего незакрытого цикла.
*/

#define LOOP_NONE 0
#define LOOP_OPEN 1
#define LOOP_CLOSE 2

/*
	Максимальное количество циклов
*/
#define LOOP_MAX 100

/*
	Размер буфера для программы
*/
#define BUFF_SIZE 0x8000

unsigned char loops[LOOP_MAX] = {LOOP_NONE};

static struct option opts[] = 
{
  {"arch", 1, 0, 'a'}, // Архитектура
	{"debug", 0, 0, 'g'},	// Отладка - отключаются оптимизации инструкций
	{"verbose", 0, 0, 'v'}, // Подробный вывод
	{"linux", 0, 0, 'l'},	// Генерация кода под Linux syscall
	{"libc", 0, 0, 'c'},	// Генерация кода, компонуемого с libc
	{"output", 1, 0, 'o'},	// Выходной файл, по умолчанию - stderr
	{"help", 0, 0, 'h'},	// Справка по использованию
	{"version", 0, 0, 'V'}, // Информация о версии
	//{"},
	{NULL, 0, 0, 0}
	
};

/*
	Ищет первый свободный номер цикла
*/
int find_free_loop()
{
	int i;
	for (i = 0; i < LOOP_MAX; i++)
		if (loops[i] == LOOP_NONE) return i;
	return -1;
}

/*
	Ищет последний незакрытый цикл
*/
int find_unclose_loop()
{
	int i;
	for (i = LOOP_MAX -1; i >= 0; i--)
		if (loops[i] == LOOP_OPEN) return i;
	return -2;
}

/*
	Справка по использованию
*/
void usage(void)
{
	printf("Usage: bfc [options] [file]\n");
	printf("If file is not specified, compiler will read from stdin\n");
	printf("Options: \n");
	printf("\t-h\t--help\tThis info\n");
	printf("\t-V\t--version\tPrint program version\n");
	printf("\t-a\t--arch\tTarget architecture (see below)\n");
	printf("\t-o outfile\t--output outfile\tRedirect output to outfile. Default is to write to stderr\n");
	printf("\t-l\t--linux\tGenerate programs for Linux syscalls\n");
	printf("\t-c\t--libc\tGenerate programs for any libc-based system (default)\n");
	printf("\t-g\t--debug\tDisable optimizations, add some debug info\n");
	printf("Supported architectures:\n");
	printf("\ti386 - 32-bit x86 (default)\n");
	printf("\tx86-64 - 64-bit x86-64\n");
	printf("\n");
	exit(0);
}

void version(void)
{
	printf("Brainfuck Compiler V" __P_VERSION__ ", " __DATE__ "\n");
	printf("\tThis program is free software; you can redistribute it\n");
	printf("\tand/or modify under the terms of GNU GPLv3 or later \n");
	printf("\tversion. This program has absolutely no warranty\n");
	printf("\n");
	printf("Please don't report any bugs. Or I will fuck you brains!\n");
	exit(0);
}

enum
{
  ARCH_X86_32 = 0,
  ARCH_X86_64
};

enum
{
  REG_A = 0,
  REG_B,
  REG_C,
  REG_D,
  REG_BP,
  REG_SP,
  REG_SI,
  REG_DI,
  REG_AL
};

enum
{
  I_PUSH = 0,
  I_POP,
  I_MOV,
  I_MOVB
};

enum
{
  SYS_exit = 1,
  SYS_read = 3,
  SYS_write = 4
};

enum
{
  CALL_getchar = 0,
  CALL_putchar = 1
};

char regs[2][9][5] = {
  // x86-32
  {
    "%eax",
    "%ebx",
    "%ecx",
    "%edx",
    "%ebp",
    "%esp",
    "%esi",
    "%edi",
    "%al"
  },
  // x86-64
  {
    "%rax",
    "%rbx",
    "%rcx",
    "%rdx",
    "%rbp",
    "%rsp",
    "%rsi",
    "%rdi",
    "%al"
  }
};

char instr[2][4][6] = {
  // x86-32
  {
    "pushl",
    "popl",
    "movl",
    "movb"
  },
  
  // x86-64
  {
    "pushq",
    "popq",
    "movq",
    "movb"
  }
};

FILE *in_file;
FILE *f;
/*
  Оптимизация включена
*/
int optim = 1;
/*
  Архитектура по умолчанию - Intel 32-бит
*/
int arch = ARCH_X86_32;
/*
	По умолчанию программа будет использовать
	вызовы стандартной библиотеки C. Это не 
	так быстро, зато будет работать на всех 
	операционных системах.
*/
int is_linux = 0;

void gen_sys_call(int a, int b, int c, int d)
{
  fprintf(f, "%s $%d, %s\n", instr[arch][I_MOV], a, regs[arch][REG_A]);
	fprintf(f, "%s $%d, %s\n", instr[arch][I_MOV], b, regs[arch][REG_B]);
	fprintf(f, "%s $%d, %s\n", instr[arch][I_MOV], d, regs[arch][REG_D]);
	fprintf(f, "int $0x80\n");
}

void gen_call(int a)
{
  // x86-32 ABI: все в стеке
  // x86-64 ABI: RDI, RSI, RDX, RCX
  if (arch == ARCH_X86_32)
  {
    fprintf(f, "%s %s\n", instr[arch][I_PUSH], regs[arch][REG_C]);
    if (a == CALL_putchar)
      fprintf(f, "%s (%s)\n", instr[arch][I_PUSH], regs[arch][REG_C]);
		fprintf(f, "call %s\n", a == CALL_putchar ? "putchar" : "getchar");
		
		if (a == CALL_putchar)
		  fprintf(f, "%s %s\n", instr[arch][I_POP], regs[arch][REG_C]);
		else if (a == CALL_getchar)
		  fprintf(f, "%s (%s)\n", instr[arch][I_POP], regs[arch][REG_C]);
		  
		fprintf(f, "%s %s\n", instr[arch][I_POP], regs[arch][REG_C]);
		fprintf(f, "%s %s, (%s)\n", instr[arch][I_MOVB], regs[arch][REG_AL], regs[arch][REG_C]);
  } else if (arch == ARCH_X86_64) {
    if (a == CALL_putchar)
    {
      fprintf(f, "xor %s, %s\n", regs[arch][REG_A], regs[arch][REG_A]);
      fprintf(f, "%s (%s), %s\n", instr[arch][I_MOVB], regs[arch][REG_C], regs[arch][REG_AL]);
      fprintf(f, "%s %s, %s\n", instr[arch][I_MOV], regs[arch][REG_A], regs[arch][REG_DI]);
    }
    // Сохраним RCX
    fprintf(f, "%s %s\n", instr[arch][I_PUSH], regs[arch][REG_C]);
    fprintf(f, "call %s\n", a == CALL_putchar ? "putchar" : "getchar");
    fprintf(f, "%s %s\n", instr[arch][I_POP], regs[arch][REG_C]);
    
    fprintf(f, "%s %s, (%s)\n", instr[arch][I_MOVB], regs[arch][REG_AL], regs[arch][REG_C]);    
  }
}

int main(int argc, char *argv[])
{
	int c;
	f = stderr;
	in_file = stdin;

	while ((c = getopt_long(argc, argv, "gva:lco:hV", opts, NULL)) != -1) 
	{
		switch (c)
		{
			case 'l':
				is_linux = 1;
				break;
			case 'o':
				f = fopen(optarg, "wb");
				break;
			case 'V':
				version();
				break;
			case 'g':
				optim = 0;
				break;
			case 'c':
				is_linux = 0;
				break;
			case 'a':
			  if(!strncmp(optarg, "i386", 4))
			  {
			    arch = ARCH_X86_32;
			    break;
			  } else if (!strncmp(optarg, "x86-64", 6)) {
			    arch = ARCH_X86_64;
			    break;
			  }
			  // не подошло ничего - выводим справку
			case 'h':
			default:
				usage();
				break;
		}
	}
	
	if(optind < argc)
	{
		in_file = fopen(argv[optind], "rb");
	}

	c = 0;
	int add_cnt = 0;	// Количество операций инкремента, накопившихся в буфере
	int mov_cnt = 0;	// Количество операций смещения головки, накопившихся в буфере
	int pos = 0;		// Позиция во входном файле
	int mov_pos = 0;	// Позиция от начала буфера - простая проверка на переход за границы
	int loop_cnt = 0;	// Номер цикла

	// C - указатель на текущую ячейку
	// A - Номер системного вызова / ничего
	// B - Дескриптор файла
	// C - Буфер для чтения/записи (тоже указатель на текущую ячейку)
	// D - Размер буфера (1 байт)

	fprintf(f, "/*\n");
	fprintf(f, "* This file was generated automatically with\n");
	fprintf(f, "* BrainFuck compiler V" __P_VERSION__ "\n");
	fprintf(f, "* You should not modify this file manually\n");
	fprintf(f, "*/\n");
	fprintf(f, "\n");

	if(is_linux)
	{
		/*
			Определяем номера системных вызовов - для читабельности
		*/
		fprintf(f, "#define SYS_exit %d\n", SYS_exit);
		fprintf(f, "#define SYS_read %d\n", SYS_read);
		fprintf(f, "#define SYS_write %d\n", SYS_write);
		fprintf(f, "#define stdin %d\n", 0);
		fprintf(f, "#define stdout %d\n", 1);
		fprintf(f, "");
	}
	fprintf(f, ".text\n");
/*
	Под Linux собираем только при помощи ассемблера, ему нужен лишь символ _start
	Для libc же нужна функция main
*/
  char *st;
	if(is_linux)
	{
	  st = "_start";
	} else {
    st = "main";
	}
  fprintf(f, ".globl %s\n", st);
  if (!optim)
  {
    // Отладочная информация
    fprintf(f, "type %s, @function\n", st);
  }
	fprintf(f, "%s:\n", st);

	fprintf(f, "%s %s\n", instr[arch][I_PUSH], regs[arch][REG_BP]);
	fprintf(f, "%s %s, %s\n", instr[arch][I_MOV], regs[arch][REG_SP], regs[arch][REG_BP]);
	fprintf(f, "%s $buffer, %s\n", instr[arch][I_MOV], regs[arch][REG_C]);

	while(!feof(in_file))
	{
		c = fgetc(in_file);
		pos++;
		// Да-да, я не придумал ничего более умного. Зато просто и работает
		switch (c)
		{
			case '+':
				if(optim)
					add_cnt++;
				else
					fprintf(f, "incb (%s)\n", regs[arch][REG_C]);
				break;
			case '-':
				if(optim)
					add_cnt--;
				else
					fprintf(f, "decb (%s)\n", regs[arch][REG_C]);
				break;
			default:
				if((add_cnt != 0)&&(optim)) 
				{
					fprintf(f, "addb $%d, (%s)\n", add_cnt, regs[arch][REG_C]);
					add_cnt = 0;
				}
		}
		switch (c)
		{
			case '<':
				if(optim)
					mov_cnt--;
				else
					fprintf(f, "dec %s\n", regs[arch][REG_C]);
                               	break;
			case '>':
				if(optim)
					mov_cnt++;
				else
					fprintf(f, "inc %s\n", regs[arch][REG_C]);
                               	break;
			default:
				if((mov_cnt != 0)&&(optim))
				{
					fprintf(f, "add $%d, %s\n", mov_cnt, regs[arch][REG_C]);
					mov_pos += mov_cnt;
/*
	TODO: Сделать ленту программы "скрученной кольцом"
*/
					if((mov_pos < 0)||(mov_pos >= BUFF_SIZE))
						printf("At %d: warning: position %d does not fit into buffer\n", pos, mov_pos);
					mov_cnt = 0;
				}
		}
		switch (c)
		{
			case ',':
/*
	Для Linux можно применить системные вызовы, а для других систем - функции библиотеки C
*/
				if(is_linux)
				{
					gen_sys_call(SYS_read, 0 /* stdin */, 0, 1);
				} else {
					gen_call(CALL_getchar);
				}
                               	break;
			case '.':
				if(is_linux)
				{
					gen_sys_call(SYS_write, 1 /* stdout */, 0, 1);
				} else {
          gen_call(CALL_putchar);
				}
                               	break;
			case '[':
				loop_cnt = find_free_loop();
				loops[loop_cnt] = LOOP_OPEN;

				fprintf(f, ".loop_%d_start:\n", loop_cnt);
				fprintf(f, "%s (%s), %s\n", instr[arch][I_MOVB], regs[arch][REG_C], regs[arch][REG_AL]);
				fprintf(f, "test %s, %s\n", regs[arch][REG_AL], regs[arch][REG_AL]);
				fprintf(f, "jz .loop_%d_end\n", loop_cnt);
                               	break;
			case ']':
				loop_cnt = find_unclose_loop();
				loops[loop_cnt] = LOOP_CLOSE;
				fprintf(f, "jmp .loop_%d_start\n", loop_cnt);
				fprintf(f, ".loop_%d_end:\n", loop_cnt);
				loop_cnt++;
                               	break;
		}
	}
	
	fprintf(f, "leave\n");
	if(is_linux)
	{
	  gen_sys_call(SYS_exit, 0, 0, 0);
	}
	fprintf(f, "ret\n");

	fprintf(f, ".bss\n");
	fprintf(f, ".globl buffer\n");
	if (!optim)
	{
	  // Отладочная информация
	  fprintf(f, ".type buffer, @object\n");
	  fprintf(f, ".size buffer, %d\n", BUFF_SIZE);
	}
	fprintf(f, "buffer:\n");
	fprintf(f, ".zero 0x%x\n", BUFF_SIZE);
	fprintf(f, ".ident \"BFC V" __P_VERSION__ ", worked fine and fast\"\n");
	fclose(f);
	fclose(in_file);
	exit(0);
}
