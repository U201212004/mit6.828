### **lab 1: Booting a PC**

#### **Part 1: PC Bootstrap**

​       介绍这一部分知识的目的就是让你能够更加熟悉x86汇编语言，以及PC启动的整个过程，而且也会首次学习使用QEMU软件来仿真xv6操作系统，并且配合GDB对操作系统的运行进行调试。

##### Getting Started with x86 assembly

​	如果你对X86汇编语言不熟悉，可以参考[汇编语言手册](http://www.delorie.com/djgpp/doc/brennan/brennan_att_inline_djgpp.html)，但是这本书的示例代码编写是基于NASM assembler(NASA 汇编器)，但是我们的课程学习是基于GNU assembler(GAS)，两者使用的汇编语法不一样，NASM使用Intel语法，GNU使用 *AT&T*语法。

**Exercise 1.**  请熟悉6.828参考页上提供的汇编语言材料。您现在不必阅读它们，但在读写x86程序集时，你需要参考其中的一些资料，推荐你使用[汇编语言手册](http://www.delorie.com/djgpp/doc/brennan/brennan_att_inline_djgpp.html)，它对我们将在jos中的GNU汇编器中使用的AT&T语法进行了详细的讲解。

答：Intel语法与*AT&T*语法的区别。

- 寄存器命名，*AT&T*在寄存器上加了%

  ```
  AT&T:  %eax
  Intel: eax
  ```

- 源地址、目标地址顺序，*AT&T*源操作数在左边，目的操作数在右边

  ```
  AT&T:  movl %eax, %ebx
  Intel: mov ebx, eax
  ```

-  寄存赋值立即数

  ```
  AT&T:  movl $_booga, %eax
  Intel: mov eax, _booga
  ```

- 寄存器后缀： `b`，`w`，`l`分别占1, 2, 4Bytes

- 寻址方式：

  - 寻址一个特定的C变量

    ```
    AT&T:  _booga
    Intel: [_booga]
    ```

  - 寻址寄存器保存的地址

    ```
    AT&T:  (%eax)
    Intel: [eax]
    ```

  - 按寄存器中值的偏移量寻址变量

    ```
    AT&T: _variable(%eax)
    Intel: [eax + _variable]
    ```

  - 在一个整数数组中寻址(按比例增加4):

    ```
    AT&T:  _array(,%eax,4)
    Intel: [eax*4 + array]
    ```

  - 用立即数做偏移

    ```
    C code: *(p+1) where p is a char *
    AT&T:  1(%eax) where eax has the value of p
    Intel: [eax + 1]
    ```

**基本内联汇编**

基本内联汇编格式非常简单：

```
asm ("statements");
asm ("nop");  nothing to do
asm ("sti");  stop interrupts
```

当涉及到像这样简单的东西时，基本的内联汇编是好的。您甚至可以将寄存器推到堆栈上，使用它们，然后再放回去。

```
asm ("pushl %eax\n\t"
     "movl $0, %eax\n\t"
     "popl %eax");
```

但是如果你改变了寄存器，并没有在你的asm语句的结尾修复东西，像这样:

```
asm ("movl %eax, %ebx");
asm ("xorl %ebx, %edx");
asm ("movl $0, _booga");
```

你的程序可能会崩溃。这是因为GCC没有被告知你的asm语句修改了ebx、edx和booga，它可能一直保存一个寄存器中，并计划以后使用它们。为此，你需要:

**扩展内联汇编**

内联程序集的基本格式基本保持不变，但是现在有了类似watcom的扩展，允许输入参数和输出参数。

```
asm ("cld\n\t"
     "rep\n\t"
     "stosl"
     : /* no output registers */ //输出部分
     : "c" (count), "a" (fill_value), "D" (dest) //输入部分
     : "%ecx", "%edi" ); //告诉寄存器，哪些寄存器被修改，不能再设用这些寄存器里面的值了
```

STOSL指令相当于将EAX中的值保存到ES:EDI指向的地址中，若设置了EFLAGS中的方向位置位(即在STOSL指令前使用STD指令)则EDI自减4，否则(使用CLD指令)EDI自增4；cld清除标志位，ecx表示复制的次数。

缩略字母的含义：

```
a        eax
b        ebx
c        ecx
d        edx
S        esi
D        edi
I        constant value (0 to 31)
q,r      dynamically allocated register (see below)
g        eax, ebx, ecx, edx or variable in memory
A        eax and edx combined into a 64-bit integer (use long longs)
```

**C语言**

可以仔细看看这个[链接](https://pdos.csail.mit.edu/6.828/2018/readings/pointers.pdf)这一部分主要讲讲c语言里面的一些用到的语法技巧。

例子1：指针强制类型转化

```c
int *x;
*((char *) (x)+1)='a';
// 就是对x指针强制类型转换，然后在该地址进行赋值。
```

例子2：数组初始化

```c
pde_t entry_pgdir[NPDENTRIES] = {
	[0] = ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,
	[960] = ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
};
//仅仅对其中的部分值进行初始化，需要有前面的[x]的索引来进行。
//后面会发现中断向量表也是这样进行的。
```

例子3：函数指针

```c
// 定义钩子函数
struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

//实例化
static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{"traceback", "traceback info", mon_kerninfo},
};

//具体的需要挂载到钩子函数上的实际函数
int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

//调用
for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
}
```

例子4：函数可变长参数

可变参数函数实现的步骤如下:

- １.在函数中创建一个va_list类型变量

- ２.使用va_start对其进行初始化
- ３.使用va_arg访问参数值
- ４.使用va_end完成清理工作

```c
#include <stdio.h>
/*要使用变长参数的宏，需要包含下面的头文件*/
#include <stdarg.h>
/*
 * getSum：用于计算一组整数的和
 * num：整数的数量
 *
 * */
int getSum(int num,...)
{
    va_list ap;//定义参数列表变量
    int sum = 0;
    int loop = 0;
    va_start(ap,num);
    /*遍历参数值*/
    for(;loop < num ; loop++)
    {
        /*取出并加上下一个参数值*/
        sum += va_arg(ap,int);
    }
    va_end(ap);
    return sum;
}

int main(int argc,char *argv[])
{
    int sum = 0;
    sum = getSum(5,1,2,3,4,5);
    printf("%d\n",sum);
    return 0;
}
```

例子5：双指针

```c
#include <stdio.h>

int xx = 10;
int xq = 233;
int main(){
    int *y = &xx;
    *y = 12;
    int **z;// = &y;
    (z) = &y; // 等价int **z=&y;
    //*z = &xq;
    printf("%d %x, %d %x, %x,  %x %x\n", xx, &xx, xq, &xq, &y, *z, **z);
    (*z) = &y;
    printf("%d %x, %d %x, %x,  %x %x\n", xx, &xx, xq, &xq, &y, *z, **z);
    //(z) = &xq;无效
    //printf("%d %x, %d &x, %x,  %x %x\n", xx, &xx, xq, &xq, &y, *z, **z);
    (*z) = &xq;
    printf("%d %x, %d %x, %x,  %x %x\n", xx, &xx, xq, &xq, &y, *z, **z);
    return 0;
}
```

一般的指针，指向向的是具体的对象。双重指向指向的是指针。

初始化有指向指针或对象的，组合后共有四种，其中仅有三种是合法的。

其中两种是常用方法：

```c
int **z;// = &y;
    (z) = &y; // 等价int **z=&y;
              //等价于 *z=y;JOS就是用这种方式传递指针的
    //*z = &xq;
    printf("%d %x, %d %x, %x,  %x %x\n", xx, &xx, xq, &xq, &y, *z, **z);

(*z) = &xq;
    printf("%d %x, %d %x, %x,  %x %x\n", xx, &xx, xq, &xq, &y, *z, **z);

//*z都是指向了一个指针，**z指向了具体的对象值
```

##### Simulating the x86

​        我们不是在真实的的个人计算机(PC)上开发操作系统，而是使用模拟器代码模拟完整计算机:模拟器的代码在真实的PC上启动。使用模拟器简化了调试;例如，您可以在x86模拟器内部设置断点，这在x86的实际计算机中是很难做到的。

​		在6.828中，我们将使用[QEMU仿真器](https://www.qemu.org/)，这是一种现代且相对快速的仿真器。虽然QEMU的内置监视器只提供有限的调试支持，但QEMU可以作为GNU调试器(GDB)的远程调试目标，我们将在本lab中使用GDB来逐步完成早期的引导启动过程。

​		在lab目录中键入make，以生成最小的6.828引导加载程序和内核。(将这里运行的代码称为“内核”有点言过其实，但我们将在之后的lab中充实它。)

```
fzh@fzh-virtual-machine:~/mit6.828/lab$ make
+ as kern/entry.S
+ cc kern/entrypgdir.c
+ cc kern/init.c
+ cc kern/console.c
+ cc kern/monitor.c
+ cc kern/printf.c
+ cc kern/kdebug.c
+ cc lib/printfmt.c
+ cc lib/readline.c
+ cc lib/string.c
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ as boot/boot.S
+ cc -Os boot/main.c
+ ld boot/boot
boot block is 390 bytes (max 510)
+ mk obj/kern/kernel.img
fzh@fzh-virtual-machine:~/mit6.828/lab$ 
```

现在可以运行QEMU模拟器了，现在提供了obj/kern/kernel.img文件，即模拟PC的“虚拟硬盘”中的内容。这个硬盘映像包含了引导加载程序(obj/boot/boot)和内核(obj/kernel)。

```
fzh@fzh-virtual-machine:~/mit6.828/lab$ make qemu
6828 decimal is XXX octal!
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

k>是命令提示符，或着说是交互式控制程序，只有两个命令可以给内核monitor：`help` and `kerninfo`。

```
K> help
help - Display this list of commands
kerninfo - Display information about the kernel
K> help
help - Display this list of commands
kerninfo - Display information about the kernel
```

​		help命令是显而易见的，我们将很快讨论kerninfo命令输出的含义，尽管很简单，但需要注意的是，这个内核monitor“直接”在模拟PC的“虚拟硬件”上运行，这意味着你可以复制obj/kern/kernel.img的内容到一个真正的硬盘的头几个扇区，并把将硬盘插入实际的个人电脑，在PC的实际屏幕上看到的东西与现在QEMU窗口中看到的完全相同。(但是，我们不建议在硬盘上有有用信息的真实机器上这样做，因为要复制内核。将img移到其硬盘的开始部分将销毁硬盘的主引导记录和第一个分区的开始部分，从而有效地导致之前在硬盘上的所有内容丢失)。

##### The PC's Physical Address Space

这一节我们将深入的探究到底PC是如何启动的。首先我们看一下通常一个PC的物理地址空间是如何布局的：

![image-20201206191407545](C:\Users\2019310661\AppData\Roaming\Typora\typora-user-images\image-20201206191407545.png)

​        第一代PC处理器是16位字长的Intel 8088处理器，这类处理器只能访问1MB的地址空间，即0x00000000~0x000FFFFF。但是这1MB也不是用户都能利用到的，只有低640KB(0x00000000~0x000A0000)的地址空间是用户程序可以使用的。如图所示。

​		而剩下的384KB的高地址空间则被保留用作其他的目的，比如(0x000A0000~0x000C0000)被用作屏幕显示内容缓冲区，其他的则被非易失性存储器(ROM)所使用，里面会存放一些固件，其中最重要的一部分就是BIOS，占据了0x000F0000~0x00100000的 64KB 地址空间。早期的pc中BIOS保存在真正的只读内存(ROM)中，但当前的pc将BIOS存储在可更新的闪存中。BIOS负责进行一些基本的系统初始化任务，比如开启显卡，检测该系统的内存大小等等工作。在初始化完成后，BIOS就会从某个合适的地方如软盘、硬盘、CD-ROM，加载操作系统。

​		虽然Intel处理器突破了1MB内存空间，在80286和80386上已经实现了16MB，4GB的地址空间，但是PC的架构必须仍旧把原来的1MB的地址空间的结构保留下来，这样才能实现向后兼容性。所以现代计算机的地址 0x000A0000~0x00100000区间是一个空洞，不会被使用。因此这个空洞就把地址空间划分成了两个部分，第一部分就是从0x00000000~0x000A0000，叫做传统内存。剩下的不包括空洞的其他部分叫做扩展内存。而对于这种32位字长处理器通常把BIOS存放到整个存储空间的顶端处。

​		由于xv6操作系统设计的一些限制，它只利用256MB的物理地址空间，即它假设用户的主机只有256MB的内存。

##### The ROM BIOS

现在我们就开始利用Qemu和gdb去探索PC机的启动过程。首先看一下如何利用这两个软件来实现对操作系统的debug。

- 首先打开一个terminal并且来到lab目录下，输入命令

    ```
make qemu-gdb
    ```

这会启动QEMU，但是QEMU会在处理器执行第一条指令之前停止，并等待来自GDB的调试连接。

- 我们再新建一个terminal，还是来到lab目录下，输入```make gdb```指令

  ```
  The target architecture is assumed to be i8086
  [f000:fff0]    0xffff0:	ljmp   $0xf000,$0xe05b
  0x0000fff0 in ?? ()
  + symbol-file obj/kern/kernel
  (gdb)
  ```

我们提供了一个.gdbinit文件，该文件设置GDB来调试早期引导期间使用的16位代码，并引导它附加到正在侦听的QEMU。

这里面我们要看下面的一行代码：

```
[f000:fff0] 0xffff0:	ljmp   $0xf000,$0xe05b
```

这条指令就是整个PC启动后，执行BIOS的第一条汇编指令。从这个输出，你可以得出以下结论:

- IBM PC从物理地址0x000ffff0开始执行，它位于为ROM BIOS保留的64KB区域的最顶端。

- PC以CS = 0xf000和IP = 0xfff0开始执行。

- 要执行的第一条指令是一条jmp指令，它跳转到分段地址CS = 0xf000和IP = 0xe05b。

　     这是运行的第一条指令，是一条跳转指令，跳转到0xfe05b地址处。至于要知道这个地址是怎么通过指令中的值计算出来的，我们需要先知道，当PC机启动时，CPU运行在实模式(real mode)下，而当进入操作系统内核后，将会运行在保护模式下(protected mode)。实模式是早期CPU，比如8088处理器的工作模式，这类处理器由于只有20根地址线，所以它们只能访问1MB的内存空间。但是CPU也在不断的发展，之后的80286/80386已经具备32位地址总线，能够访问4GB内存空间，为了能够很好的管理这么大的内存空间，保护模式被研发出来。所以现代处理器都是工作在保护模式下的。但是为了实现向后兼容性，即原来运行在8088处理器上的软件仍旧能在现代处理器上运行，所以现代的CPU都是在启动时运行于实模式，启动完成后运行于保护模式。BIOS就是PC刚启动时运行的软件，所以它必然工作在实模式。

​	0xffff0是BIOS结束之前的16个字节(0x100000)。因此，我们不应该对BIOS做的第一件事是jmp向后回到BIOS中较早的位置感到惊讶;毕竟，仅仅16个字节就能完成多少任务呢?

Exercise 2： 使用GDB的'si'命令，去追踪ROM BIOS几条指令，并且试图去猜测，它是在做什么。但是不需要把每个细节都弄清楚。

当BIOS运行时，它设置一个中断描述符表并初始化各种设备，比如VGA显示器。

​		在初始化PCI总线和BIOS知道的所有重要设备之后，它将搜索可引导的设备，如软盘、硬盘驱动器或CD-ROM。最终，当它找到一个可引导的磁盘时，BIOS从磁盘中读取引导加载程序并将控制权交给它。

