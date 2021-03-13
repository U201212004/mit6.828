#### Part3: The Kernel

​		现在，我们将开始更详细地研究最小JOS内核。与引导加载程序一样，内核从一些汇编语言代码开始，这些代码进行设置，以便C语言代码能够正确执行。

##### 使用虚拟内存来解决位置依赖问题

​		当你检查引导加载程序的链接地址和加载地址时，它们完全相同，但是内核的链接地址(由objdump打印)和它的加载地址之间存在(相当大的)差异，可以回去验证一下。链接器链接内核比链接加载程序更复杂，因此链接文件位于kern/kernel.ld中。

​		操作系统内核通常喜欢链接在非常高的虚拟地址上运行，例如0xf0100000，以便把处理器虚拟地址空间的较低部分留给用户程序使用。这种安排的原因在下一个lab会更清楚。

​		许多机器没有地址0xf0100000的物理内存，所以我们不能将内核存储在那里。然而，我们将使用处理器的内存管理硬件来映射虚拟地址0xf0100000(链接地址)到物理地址0x00100000(内核实际在内存中地址)，这样，尽管内核的虚拟地址足够高，可以为用户进程留下足够的地址空间，但它将被加载到PC RAM中1MB的物理内存中，就在BIOS ROM上方。这种方法要求PC至少有几兆的物理内存(这样物理地址0x00100000就可以工作)。

​		事实上，在下一个lab中，我们将把PC的整个底部256MB的物理地址空间，从物理地址0x00000000到0x0fffffff，分别映射到虚拟地址0xf0000000到0xffffffff。现在您应该看到为什么JOS只能使用前256MB的物理内存。

​		现在，我们只映射前4MB的物理内存，这将足以让我们启动并运行。我们使用kern/entrypgdir.c中手写的、静态初始化的页目录和页表来实现这一点。现在，您不必了解它如何工作的细节，只需了解它所实现的效果。在kern /entry.S设置CR0_PG标志之前，内存引用被视为物理地址(严格地说，它们是线性地址，但是boot/boot.S建立了一个从线性地址到物理地址的对应映射，意思是段:偏移量的映射，应为每个段的起始地址都是0x00000000，偏移量就是物理地址)。一旦设置了CR0_PG，内存引用就是虚拟地址，由虚拟内存硬件将其转换为物理地址。entry_pgdir将0xf0000000-0xf0400000范围内的虚拟地址转换为0x00000000-0x00400000的物理地址，以及虚拟地址0x00000000-0x00400000到物理地址0x00000000-0x00400000。任何不在这两个范围内的虚拟地址都将导致硬件异常，由于我们还没有设置中断处理，这将导致QEMU宕机并退出。

**Exercise 7：**使用Qemu和GDB去追踪JOS内核文件，并且停止在movl %eax, %cr0指令前。此时看一下内存地址0x00100000以及0xf0100000处分别存放着什么。然后使用stepi命令执行完这条命令，再次检查这两个地址处的内容。确保你真的理解了发生了什么。

如果这条指令movl %eax, %cr0并没有执行，而是被跳过，那么第一个会出现问题的指令是什么？我们可以通过把entry.S的这条语句加上注释来验证一下。

答：

​	bootloader将内核加载到物理内存0x100000处，并通过```((void (*)(void)) (ELFHDR->e_entry))();```代码将控制权交给内存，那么问题来了，这行代码跳转到哪里了，是0x10000c还是0xf010000C，不用说，肯定是0x10000c，因为这个时候分页机制还没建立，但是内核的链接地址从Part2可以得出是0xf0100000以上的，那么处理器是怎么知道要去0x10000C处运行的呢？

```
fzh@fzh-virtual-machine:~/mit6.828/lab$ objdump -f obj/kern/kernel
obj/kern/kernel：     文件格式 elf32-i386
体系结构：i386， 标志 0x00000112：
EXEC_P, HAS_SYMS, D_PAGED
起始地址 0x0010000c  //这个就是entry point,也是(ELFHDR->e_entry))()函数实际跳转的地方
```

```
fzh@fzh-virtual-machine:~/mit6.828/lab$ objdump -x obj/kern/kernel
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00001871  f0100000  00100000  00001000  2**4
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
```

​		原来在kern/entry.S中，代码是这样设置的，entry的地址(这里所有的地址都是链接地址)是0xf010000C，但是_start通过RELOC(x)宏将程序的入口点强行改为0x10000c，如果不这样改的话，bootloader将跳转到0xf010000C执行，这样无法正常运行。

```c
 18 #define RELOC(x) ((x) - KERNBASE)
 //....
 36 # '_start' specifies the ELF entry point.  Since we haven't set up
 37 # virtual memory when the bootloader enters this code, we need the
 38 # bootloader to jump to the *physical* address of the entry point.
 39 .globl      _start
 40 _start = RELOC(entry)
 41 
 42 .globl entry
 43 entry:
 44     movw    $0x1234,0x472           # warm boot
```

kern/entrypgdir.c中的手动分页机制：

```c
__attribute__((__aligned__(PGSIZE)))
pde_t entry_pgdir[NPDENTRIES] = {
    // 映射虚拟地址 [0, 4MB) 到物理地址 [0, 4MB)
    [0]
        = ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,      
    // 映射虚拟地址 [KERNBASE, KERNBASE+4MB) 到物理地址 [0, 4MB)
    // PDXSHIFT是22，即取地址最高10位作为页目录的索引。 
    //指向同一个页表机制
    [KERNBASE>>PDXSHIFT]
        = ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
};

__attribute__((__aligned__(PGSIZE)))
pte_t entry_pgtable[NPTENTRIES] = {
    0x000000 | PTE_P | PTE_W,     //对应第一个4kB页面
    0x001000 | PTE_P | PTE_W,     //对应第二个4kB页面
    0x002000 | PTE_P | PTE_W,
    ......
    0x3ff000 | PTE_P | PTE_W,     //对应第1024个4kB页面 即4MB空间
}
```

回过头看kern/entry.S文件：

```c
 42 .globl entry
 43 entry:
 44     movw    $0x1234,0x472           # warm boot
 46     # We haven't set up virtual memory yet, so we're running from
 47     # the physical address the boot loader loaded the kernel at: 1MB
 48     # (plus a few bytes).  However, the C code is linked to run at
 49     # KERNBASE+1MB.  Hence, we set up a trivial page directory that
 50     # translates virtual addresses [KERNBASE, KERNBASE+4MB) to
 51     # physical addresses [0, 4MB).  This 4MB region will be
 52     # sufficient until we set up our real page table in mem_init
 53     # in lab 2.
 55     # Load the physical address of entry_pgdir into cr3.  entry_pgdir
 56     # is defined in entrypgdir.c.
 57     movl    $(RELOC(entry_pgdir)), %eax
 58     movl    %eax, %cr3
 59     # Turn on paging.
 60     movl    %cr0, %eax
 61     orl $(CR0_PE|CR0_PG|CR0_WP), %eax
 62     movl    %eax, %cr0
 64     # Now paging is enabled, but we're still running at a low EIP
 65     # (why is this okay?).  Jump up above KERNBASE before entering
 66     # C code.
 67     mov $relocated, %eax
 68     jmp *%eax
 69 relocated:
 70 
 71     # Clear the frame pointer register (EBP)
 72     # so that once we get into debugging C code,
 73     # stack backtraces will be terminated properly.
 74     movl    $0x0,%ebp           # nuke frame pointer
 75 
 76     # Set the stack pointer
 77     movl    $(bootstacktop),%esp
 78 
 79     # now to C code
 80     call    i386_init
```

​		cr3寄存器保存entry_pgdir的页目录表地址，cr0开启分页。现在需要跳到KERNBASE以上去运行，这里relocated的地址(链接地址)是在0xf0100000以上的，jmp*%eax表示跳转到%eax保存的内容上，即relocated的链接地址，所以这一步将跳到KERNBASE以上去运行。

​		现在来回答这个练习，我们可以首先设置断点到0x10000C处，因为我们在之前的练习中已经知道了，0x10000C是内核文件的入口地址。 然后我们从这条指令开始一步步运行，直到碰到movl %eax, %cr0指令。在这条指令运行之前，地址0x00100000和地址0xf0100000两处存储的内容是：

```
(gdb) x/8xb 0x100000
0x100000:	0x02	0xb0	0xad	0x1b	0x00	0x00	0x00	0x00
(gdb) x/8xb 0xf0100000
0xf0100000 <_start+4026531828>:	0x00	0x00	0x00	0x00	0x00	0x00	0x00	0x00
//运行分页指令之后
(gdb) x/8xb 0x100000
0x100000:	0x02	0xb0	0xad	0x1b	0x00	0x00	0x00	0x00
(gdb) x/8xb 0xf0100000
0xf0100000 <_start+4026531828>:	0x02	0xb0	0xad	0x1b	0x00	0x00	0x00	0x00
```

我们会发现两处存放的值已经一样了！ 可见原本存放在0xf0100000处的内容，已经被映射到0x00100000处了。

​		第二问需要我们把entry.S文件中的%movl %eax, %cr0这句话注释掉，重新编译内核。我们需要先make clean，然后把%movl %eax, %cr0这句话注释掉，重新编译。 再次用qemu仿真，并且设置断点到0x10000C处，开始一步步执行。通过一步步查询发现了出现错误的一句。

```
=> 0x10001a:	mov    %eax,%cr3
0x0010001a in ?? ()
(gdb) si
=> 0x10001d:	mov    %cr0,%eax
0x0010001d in ?? ()
(gdb) si
=> 0x100020:	or     $0x80010001,%eax
0x00100020 in ?? ()
(gdb) si
=> 0x100025:	mov    $0xf010002c,%eax
0x00100025 in ?? ()
(gdb) si
=> 0x10002a:	jmp    *%eax
0x0010002a in ?? ()
(gdb) si
=> 0xf010002c <relocated>:	add    %al,(%eax)
relocated () at kern/entry.S:74
74		movl	$0x0,%ebp			# nuke frame pointer
(gdb) si
Remote connection closed
```

其中在0x10002a处的jmp指令，要跳转的位置是0xf010002C，由于没有进行分页管理，此时不会进行虚拟地址到物理地址的转化。所以报出错误，下面是make qemu-gdb这个窗口中出现的信息。

```
qemu: fatal: Trying to execute code outside RAM or ROM at 0xf010002c
```

#### Formatted Printing to the Console(格式化输出到控制台)

​		大多数人认为像printf()这样的函数是理所当然的，有时甚至认为它们是C语言的“原语”。但是在操作系统内核中，我们必须自己实现所有的I/O。

​		通读kern/printf.c，lib/printfmt.c，kern.控制台console.c，确保你了解它们的关系。在以后的lab中，我们将会清楚为什么printfmt.c被放在独立的lib目录中。

**Exercise 8.** 我们省略了一小段代码——使用“%o”形式的模式打印八进制数所需的代码。查找并填充此代码片段。

答：

lib/printfmt.c：

```c
246 void
247 printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...)
248 {   
249     va_list ap;
250     
251     va_start(ap, fmt);
252     vprintfmt(putch, putdat, fmt, ap);
253     va_end(ap);
254 }
```

lib/printfmt.c调用vprintfmt函数：

```c
void vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap)
```

vprintfmt函数有4个函数参数：

- ``` void (*putch)(int, void*)```：

  

kern/printf.c：

```c
  9 static void
 10 putch(int ch, int *cnt)
 11 {
 12     cputchar(ch);
 13     *cnt++;
 14 }
 15 
 16 int
 17 vcprintf(const char *fmt, va_list ap)
 18 {
 19     int cnt = 0;
 20 
 21     vprintfmt((void*)putch, &cnt, fmt, ap);
 22     return cnt;
 23 }
 24 
 25 int
 26 cprintf(const char *fmt, ...)
 27 {
 28     va_list ap;
 29     int cnt;
 30 
 31     va_start(ap, fmt);
 32     cnt = vcprintf(fmt, ap);
 33     va_end(ap);
 34 
 35     return cnt;
 36 }
```

上述可知，putch函数调用cputchar函数，cputchar在console.c中定义：

```c
455 void
456 cputchar(int c)
457 {
458     cons_putc(c);
459 }

431 // output a character to the console
432 static void
433 cons_putc(int c)
434 {
435     serial_putc(c);
436     lpt_putc(c);
437     cga_putc(c);
438 }
```

```
 16 #define CRT_ROWS    25
 17 #define CRT_COLS    80
 18 #define CRT_SIZE    (CRT_ROWS * CRT_COLS)
```

```c
162 static void
163 cga_putc(int c)
164 {
165     // if no attribute given, then use black on white
166     if (!(c & ~0xFF))
167         c |= 0x0700;
168 
169     switch (c & 0xff) {
170     case '\b':
171         if (crt_pos > 0) {
172             crt_pos--;
173             crt_buf[crt_pos] = (c & ~0xff) | ' ';
174         }
175         break;
176     case '\n':
177         crt_pos += CRT_COLS;
178         /* fallthru */
179     case '\r':
180         crt_pos -= (crt_pos % CRT_COLS);
181         break;
182     case '\t':
183         cons_putc(' ');
184         cons_putc(' ');
185         cons_putc(' ');
186         cons_putc(' ');
187         cons_putc(' ');
188         break;
189     default:
190         crt_buf[crt_pos++] = c;     /* write the character */
191         break;
192     }
193 
194     // What is the purpose of this?
       //超过屏幕显示范围，整体向上移动一行
195     if (crt_pos >= CRT_SIZE) {
196         int i;
197 
198         memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
199         for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
200             crt_buf[i] = 0x0700 | ' ';//把最后一行设置为空格
201         crt_pos -= CRT_COLS;     //
202     }
203 
204     /* move that little blinky thing */
205     outb(addr_6845, 14);
206     outb(addr_6845 + 1, crt_pos >> 8);
207     outb(addr_6845, 15);
208     outb(addr_6845 + 1, crt_pos);
209 }
```

​		cputchar这个程序的功能根据名称就能才出来了，肯定是把字符输出到cga设备上面，即计算机的显示屏。```cga_putc(int c)```表示打印一个字符到控制台，crt_buf为一块连续内存中的起始地址(缓冲区)，crt为当前光标位置，CRT_ROWS表示屏幕能输出最大行数，CRT_COLS表示屏幕最大能输出的列数，CRT_SIZE表示最大能输出的字符数。\b表示退格，所以要把缓冲区最后一个字节的指针crt_pos减一；如果c为'\n'，则crt_pos加上每行长度CRT_COLS，没有break，继续向下执行，减去crt_pos % CRT_COLS)，光标移到行首，如果c为'\r'，移到行首，如果不是特殊字符，那么就把字符的内容直接输入到缓冲区。

​		而switch之后的if判断语句的功能应该是保证缓冲区中的最后显示出去的内容的大小不要超过显示的大小界限CRT_SIZE。最后四句则是把缓冲区的内容输出给显示屏。

能够回答以下问题：

- 解释一下printf.c和console.c两个之间的关系。console.c包含了哪些子函数？这些子函数是怎么被printf.c所利用的？

答：```cprintf(const char *fmt, ...)```调用```vcprintf(const char *fmt, va_list ap)```，接着调用```vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap)```，vprintfmt是解释打印规则的主要函数，利用putch函数打印字符，```putch(int ch, int *cnt)```调用console.c的```cputchar(int c)```，调用cga_putc(c)函数，负责打印字符到控制台。

-  解释console.c的以下代码：

```
1      if (crt_pos >= CRT_SIZE) {
2              int i;
3              memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
4              for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
5                      crt_buf[i] = 0x0700 | ' ';
6              crt_pos -= CRT_COLS;
7      }
```

答：超过屏幕显示范围CRT_SIZE，整体向上移动一行，将最后1行的内容用黑色的空格塞满。将空格字符、0x0700进行或操作的目的是让空格的颜色为黑色。最后更新crt_pos的值。总结：这段代码的作用是当屏幕写满内容时将其上移1行，并将最后一行用黑色空格塞满。

- 逐步跟踪以下代码的执行：

```
int x = 1, y = 3, z = 4;
cprintf("x %d, y %x, z %d\n", x, y, z);
```

在调用cprintf()时，fmt指向什么？ap指向什么？

按照执行的顺序列出所有对cons_putc, va_arg和vcprintf的调用。对于cons_putc，列出它所有的输入参数。对于va_arg，列出ap在执行完这个函数后的和执行之前的指向变化。对于vcprintf列出它的两个输入参数的值。

答：首先fmt自然指向的是显示信息的格式字符串，那么在这段代码中，它指向的就是"x %d, y %x, z %d\n"字符串。而ap是va_list类型的。我们之前已经介绍过，这个类型专门用来处理输入参数的个数是可变的情况。所以ap会指向所有输入参数的集合。fmt指向格式化字符串"x %d, y %x, z %d\n"的内存地址（这个字符串是存储在栈中吗？），ap指向第一个要打印的参数的内存地址，也就是x的地址。

- 运行以下代码：

```c
unsigned int i = 0x00646c72;
cprintf("H%x Wo%s", 57616, &i);
```

输出是什么?解释如何一步一步的得到这个输出。下面是一个将字节映射到字符的ASCII表。

上述输出结果的根据是x86采用小端模式，如果采用大端模式，要得到相同的输出，需要将57616更改为一个不同的值吗？

答：首先看下第一个%x，指的是要按照16进制输出第一个参数，第一个参数的值是57616，它对应的16进制的表示形式为e110，所以前面就变成的He110。看下一个%s，输出参数所指向的字符串。参数是&i，是变量i的地址，所以应该输出的是变量i所在地址处的字符串。

​		由于x86是小端模式，代表字的最高位字节存放在最高位字节地址上。假设i变量的地址为0x00，那么i的4个字节的值存放在0x00，0x01，0x02，0x03四处。由于是小端存储，所以0x00处存放0x72('r')，0x01处存放0x6c('l')，0x02处存放0x64('d')，0x03处存放0x00('\0'). 所以在cprintf将会从i的地址开始一个字节一个字节遍历，正好输出 "World"。如果是大端字节序，i的值要修改为0x726c6400，而57616这个值不用修改。

- 在下面的代码中，'y='之后将打印什么?(注意:答案不是一个特定的值。)为什么会这样呢?

```
cprintf("x=%d y=%d", 3);
```

答：打印出来的y的值应该是栈中存储x的位置后面4字节代表的值。因为当打印出x的值后，va_arg函数的ap指针指向x的最后一个字节的下一个字节。因此，不管调用cprintf传入几个参数，在解析到"%d"时，va_arg函数就会取当前指针指向的地址作为int型整数的指针返回。

- 假设GCC改变了它的调用约定，改为参数从左到右压栈，最后一个参数被推到最后。为支持参数数目可变需要怎样修改cprintf函数？

答：参数从左到右压栈，第一个参数先入栈，那么得到第一个参数的地址，即格式化字符串的位置，只要在第一个参数由加改为减。

```
#define va_start(ap,v) ( ap = (va_list)&v + _INTSIZEOF(v) )
#define va_arg(ap,t) ( *(t *)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)) )
#define va_end(ap) ( ap = (va_list)0 )
//修改后
#define va_start(ap,v) ( ap = (va_list)&v - _INTSIZEOF(v) )
#define va_arg(ap,t) ( *(t *)((ap -= _INTSIZEOF(t)) )
#define va_end(ap) ( ap = (va_list)0 )
```

将其接口改为：

```C
cprintf(..., int n, const char* fmt)
```

其中`n`可变参数的个数。

```C
cprintf(..., const char* fmt)
```

------

### The Stack

​		在本lab的最后一个练习中，我们将更详细地探索C语言在x86上使用堆栈的方式，在这个过程中，编写一个有用的新内核监视函数，输出堆栈的回溯：由一系列被保存到堆栈的IP寄存器的值组成的，之所以会产生这一系列被保存的IP寄存器的值，是因为我们执行了一个程序，程序中包括一系列嵌套的call指令。

**Exercise 9.** 判断一下操作系统内核是从哪条指令开始初始化它的堆栈空间的，以及这个堆栈位于内存的哪个位置？堆栈指针又是指向这块被保留的区域的哪一端的呢？

答：

在entry.S中我们可以看到它最后一条指令是要调用i386_init()子程序。可见到i386_init子程序时，内核的堆栈应该已经设置好了。初始的栈所在位置为一个.data段：

```c
76     # Set the stack pointer
77     movl    $(bootstacktop),%esp
```

```
 86 .data
 90     .p2align    PGSHIFT     # force page alignment
 91     .globl      bootstack
 92 bootstack:
 93     .space      KSTKSIZE
 94     .globl      bootstacktop
 95 bootstacktop:
```

采用`.space KSTKSIZE`为栈静态分配空间，栈指针初始指向`bootstacktop`，即该栈空间的地址最高处。在.data首先分配了KSTKSIZE这么多的存储空间，专门用于堆栈，这个KSTKSIZE = 8 * PGSIZE = 8 * 4096 = 32KB。```mov $0xf0110000,%esp```.

​		x86堆栈指针(esp寄存器)指向当前正在使用的堆栈上的最低位置，在为堆栈保留的区域中，esp位置以下的所有内容都是空闲的，将值压入堆栈包括减小堆栈指针，然后将值写入堆栈指针所指向的位置。从堆栈弹出一个值(读取堆栈指针指向的值)，然后增加堆栈指针。在32位模式下，堆栈只能存放32位的值，esp总是能被4整除。

​	相反，ebp(基指针)寄存器主要是通过软件约定与堆栈相关联的。在进入C函数时，最先要运行的代码就是先把之前调用这个子程序的程序的ebp寄存器的值压入堆栈中保存起来，然后把ebp寄存器的值更新为当前esp寄存器的值。此时就相当于为这个子程序定义了它的ebp寄存器的值，也就是它栈帧的一个边界。只要所有的程序都遵循这样的编程规则，那么当我们运行到程序的任意一点时。我们可以通过在堆栈中保存的一系列ebp寄存器的值来回溯，弄清楚是怎样的一个函数调用序列使我们的程序运行到当前的这个点。

**Exercise 10. **为了能够更好的了解在x86上的C程序调用过程的细节，我们首先找到在obj/kern/kern.asm中test_backtrace子程序的地址，设置断点，并且探讨一下在内核启动后，这个程序被调用时发生了什么。对于这个循环嵌套调用的程序test_backtrace，它一共压入了多少信息到堆栈之中。并且它们都代表什么含义？

答：递归调用自身时，`test_backtrace`先将`x-1`压栈，再将返回地址压栈，再将`%ebp`压栈，共3个32位数。

```c
 10 // Test the stack backtrace function (lab 1 only)
 11 void
 12 test_backtrace(int x)
 13 {
 14     cprintf("entering test_backtrace %d\n", x);
 15     if (x > 0)
 16         test_backtrace(x-1);
 17     else
 18         mon_backtrace(0, 0, 0);
 19     cprintf("leaving test_backtrace %d\n", x);
 20 }
```

```c
 22 void
 23 i386_init(void)
 24 {
 25     extern char edata[], end[];
 26 
 27     // Before doing anything else, complete the ELF loading process.
 28     // Clear the uninitialized global data (BSS) section of our program.
 29     // This ensures that all static/global variables start out zero.
 30     memset(edata, 0, end - edata);
 31 
 32     // Initialize the console.
 33     // Can't call cprintf until after we do this!
 34     cons_init();
 35 
 36     cprintf("6828 decimal is %o octal!\n", 6828);
 37 
 38     // Test the stack backtrace function (lab 1 only)
 39     test_backtrace(5);
 41     // Drop into the kernel monitor.
 42     while (1)
 43         monitor(NULL);
 44 }
```

​		上面的练习有助于你实现堆栈回溯函数```mon_backtrace()```，这个函数的原型定义在```kern/monitor.c```中。你可以完全用C来完成，但是您可能会发现inc/x86.h中的read_ebp()函数很有用。您还必须将这个新函数挂接到内核监视器的命令列表中，以便用户可以交互式地调用它。

backtrace函数应该以如下格式显示函数调用帧列表:

```c
Stack backtrace:
  ebp f0109e58  eip f0100a62  args 00000001 f0109e80 f0109e98 f0100ed2 00000031
  ebp f0109ed8  eip f01000d6  args 00000000 00000000 f0100058 f0109f28 00000061
  ...
```

​		每行包含ebp、eip和args，ebp值表示进入该函数使用的堆栈的基指针；列出的eip值是函数的返回指令指针；args后列出的5个十六进制值是所讨论的函数的前5个参数，它们将在调用函数之前被压入堆栈。当然，如果调用函数时参数少于5个，那么并不是所有的5个值都有用。(为什么回溯代码不能检测实际有多少参数?这个限制怎么能被修正呢?)

​		打印的第一行反映了当前正在执行的函数，即```mon_backtrace```本身，第二行反映了调用```mon_backtrace```函数的函数，第三行依次类推，你应该打印所有堆栈帧，通过研究```kern/entry.S```，你会发现有一种简单的方法来告诉你什么时候该停下来。

​		以下是你在《K&R》第5章中读到的一些特别的要点，值得记住，以备以后的练习和将来的lab。

- 如果int* p = (int*)100，那么(int)p + 1和(int)(p + 1)是不同的数:第一个是101，第二个是104。当向指针添加一个整数时，如在第二种情况中，这个整数隐式地乘以指针所指向的对象的大小。
- p[i]被定义为与*(p+i)相同，指的是p在内存中指向的第i个对象。
- &p[i]和(p+i)是一样的，生成了内存中p指向的第i个对象的地址。

​        尽管大多数C程序从来不需要在指针和整数之间进行强制转换，但操作系统经常需要这样做，当你看到一个涉及内存地址的加法时，问问你自己它是整数加法还是指针加法。

**Exercise 11.** 实现backtrace函数，用上述例子中相同的格式，当你认为你代码是对的，运行make grade命令查看结果输出格式是否与期望的格式相同，如果不相同，修改你的代码，在你递交了lab1代码之后，欢迎你修改backtrace函数的输出格式。

<img src="C:\Users\2019310661\AppData\Roaming\Typora\typora-user-images\image-20210109194110840.png" alt="image-20210109194110840" style="zoom:80%;" />

答：先从左往右将函数参数入栈，再执行call命令，将父程序返回地址入栈，再保存父程序的ebp，设置子程序的ebp，建立一个堆栈空间。

```c
 57 int
 58 mon_backtrace(int argc, char **argv, struct Trapframe *tf)
 59 {
 60     uint32_t ebp, *ptr_ebp;
 61     ebp = read_ebp();  //read current ebp value
 62     cprintf("Stack backtrace:\n");
 63     while(ebp != 0){
 64         ptr_ebp = (uint32_t *)ebp;  //transform to a pointer
 65         cprintf("\tebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
 66             ebp, ptr_ebp[1], ptr_ebp[2], ptr_ebp[3], ptr_ebp[4], ptr_ebp[5], ptr_ebp[6]);
 67             ebp = *ptr_ebp;
 68     }
 69 
 70     return 0;
 71 }
```

eip即为ptr_ebp[1]，即当前函数栈ebp上方的返回地址。

ptr_ebp[2], ptr_ebp[3]….是函数的输入参数

ebp = *ptr_ebp  表示上一个ebp值

%08x表示宽度为8，不够用0补齐的16进制的数。

主要是根据提示来改写 kern/monitor.c，重点用到的三个tricks：

a)    利用read_ebp() 函数获取当前ebp值;

b)    利用 ebp 的初始值0判断是否停止，因为在entry.S文件中有一行```movl    $0x0,%ebp ```;

c)    利用数组指针运算来获取 eip 以及 args。

------

​		acktrace函数应该给出了堆栈上导致mon_backtrace()函数调用者的地址，然而，在实践中，您通常希望知道与这些地址对应的函数名。例如，您可能想知道哪些函数可能包含导致内核崩溃的错误。

​		为了帮助您实现这个功能，我们提供了debuginfo_eip()函数，该函数在符号表中查找eip，并返回该地址的调试信息。这个函数在kern/kdebug.c中定义。

**Exercise 12.**修改你的堆栈backtrace()函数以显示eip，函数名，源文件名，与eip对应的行号。

在```debuginfo_eip```中，```__STAB_*```来自哪里?这个问题的答案很长;为了帮助你找到答案，可以按以下步骤做:

- 查看```kern/kernel.ld```的```__STAB_ *```；
- 运行```objdump -h obj/kern/kernel```;
- 运行```objdump -G obj/kern/kernel```;
- 在lab目录下执行运行```gcc -pipe -nostdinc -O2 -fno-builtin -I. -MD -Wall -Wno-format -DJOS_KERNEL -gstabs -c -S kern/init.c```;

- 确认boot loader在加载内核时是否把符号表也加载到内存中。

通过在代码中调用```stab_binsearch```来查找地址的行号，完成```debuginfo_eip```的实现。

答：

1、```__STAB_BEGIN__, __STAB_END__, __STABSTR_BEGIN__, __STABSTR_END__```等符号均在```kern/kern.ld```文件定义，它们分别代表```.stab```和```.stabstr```这两个段开始与结束的地址。

```c
/* Include debugging information in kernel memory */
.stab : {
    PROVIDE(__STAB_BEGIN__ = .);
    *(.stab);
    PROVIDE(__STAB_END__ = .);
    BYTE(0)		/* Force the linker to allocate space
               for this section */
}
.stabstr : {
    PROVIDE(__STABSTR_BEGIN__ = .);
    *(.stabstr);
    PROVIDE(__STABSTR_END__ = .);
    BYTE(0)		/* Force the linker to allocate space
               for this section */
```

2、执行```objdump -h obj/kern/kernel```命令，结果显示如下：

这5个段是从加载地址起点开始依次放置的。```STAB_BEGIN=0xf0101fdc，STAB_END=0xf0101fdc+0x38e9-1=0xf01058c5，STABSTR_BEGIN=0xf01058c5，STABSTR_END=0xf01058c5+0x18f4-1=0xf0108000```当然要考虑对齐，不是严格相等。

```shell
节：
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00001831  f0100000  00100000  00001000  2**4
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .rodata       0000079c  f0101840  00101840  00002840  2**5
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .stab         000038e9  f0101fdc  00101fdc  00002fdc  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .stabstr      000018f4  f01058c5  001058c5  000068c5  2**0
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  4 .data         0000a300  f0108000  00108000  00009000  2**12
                  CONTENTS, ALLOC, LOAD, DATA
  5 .bss          00000648  f0112300  00112300  00013300  2**5
                  CONTENTS, ALLOC, LOAD, DATA
  6 .comment      00000035  00000000  00000000  00013948  2**0
                  CONTENTS, READONLY
```

3、执行```objdump -G obj/kern/kernel | more```命令

```
Symnum n_type n_othr n_desc n_value  n_strx String

-1     HdrSym 0      1213   000018f3 1     
0      SO     0      0      f0100000 1      {standard input}
1      SOL    0      0      f010000c 18     kern/entry.S
2      SLINE  0      44     f010000c 0      
3      SLINE  0      57     f0100015 0      
4      SLINE  0      58     f010001a 0      
5      SLINE  0      60     f010001d 0      
6      SLINE  0      61     f0100020 0      
7      SLINE  0      62     f0100025 0      
8      SLINE  0      67     f0100028 0      
9      SLINE  0      68     f010002d 0      
10     SLINE  0      74     f010002f 0      
11     SLINE  0      77     f0100034 0      
12     SLINE  0      80     f0100039 0      
13     SLINE  0      83     f010003e 0      
14     SO     0      2      f0100040 31     kern/entrypgdir.c
15     OPT    0      0      00000000 49     gcc2_compiled.
16     LSYM   0      0      00000000 64     int:t(0,1)=r(0,1);-2147483648;2147483647;
17     LSYM   0      0      00000000 106    char:t(0,2)=r(0,2);0;127;
18     LSYM   0      0      00000000 132    long int:t(0,3)=r(0,3);-2147483648;2147483647;
19     LSYM   0      0      00000000 179    unsigned int:t(0,4)=r(0,4);0;4294967295;
20     LSYM   0      0      00000000 220    long unsigned int:t(0,5)=r(0,5);0;4294967295;
21     LSYM   0      0      00000000 266    __int128:t(0,6)=r(0,6);0;-1;
22     LSYM   0      0      00000000 295    __int128 unsigned:t(0,7)=r(0,7);0;-1;
23     LSYM   0      0      00000000 333    long long int:t(0,8)=r(0,8);-0;4294967295;
24     LSYM   0      0      00000000 376    long long unsigned int:t(0,9)=r(0,9);0;-1;
25     LSYM   0      0      00000000 419    short int:t(0,10)=r(0,10);-32768;32767;
26     LSYM   0      0      00000000 459    short unsigned int:t(0,11)=r(0,11);0;65535;
27     LSYM   0      0      00000000 503    signed char:t(0,12)=r(0,12);-128;127;
28     LSYM   0      0      00000000 541    unsigned char:t(0,13)=r(0,13);0;255;
29     LSYM   0      0      00000000 578    float:t(0,14)=r(0,1);4;0;
```

```Stab```在```lab/inc/stab.h```文件中定义：

```c
 43 struct Stab {
 44     uint32_t n_strx;    // index into string table of name
 45     uint8_t n_type;         // type of symbol
 46     uint8_t n_other;        // misc info (usually empty)
 47     uint16_t n_desc;        // description field
 48     uintptr_t n_value;  // value of symbol
 49 };
```

```
 12 // The constants below define some symbol types used by various debuggers
 13 // and compilers.  JOS uses the N_SO, N_SOL, N_FUN, and N_SLINE types.
 17 #define N_FUN       0x24    // procedure name
 27 #define N_SO        0x64    // main source file name
 23 #define N_SLINE     0x44    // text segment line number
 30 #define N_SOL       0x84    // included source file name
```

N_SO表示源文件的名字：

```
68     SO     0      2      f0100040 2712   kern/init.c
142    SO     0      2      f0100177 2970   kern/console.c    
```

查看obj/kernel.asm得到：

```c
70 f0100040 <test_backtrace>:
71 #include <kern/console.h>
72 
73 // Test the stack backtrace function (lab 1 only)
74 void
75 test_backtrace(int x)
//0xf0100040正是kern/init.c文件缩放在虚拟地址中起始位置
 253 f0100177 <serial_proc_data>:
 254 
 255 static bool serial_exists;
 256 
 257 static int
 258 serial_proc_data(void)
 259 {
 260 f0100177:   55                      push   %ebp
 261 f0100178:   89 e5                   mov    %esp,%ebp 
//f0100177正是kern/console.c文件缩放在虚拟地址中起始位置
```

N_SOL表示随后出现的变量、函数等符号所要参考的源文件.

例如在kern/console.c的函数serial_proc_data中：

```c
 50 static int
 51 serial_proc_data(void)
 52 {
 53     if (!(inb(COM1+COM_LSR) & COM_LSR_DATA))
 54         return -1;
 55     return inb(COM1+COM_RX);
 56 }
```

```
173    FUN    0      0      f0100177 3012   serial_proc_data:f(0,1)
174    SLINE  0      52     00000000 0      
175    SOL    0      0      f010017a 2985   ./inc/x86.h
176    SLINE  0      16     00000003 0      
177    SOL    0      0      f0100180 2970   kern/console.c
178    SLINE  0      53     00000009 0      
179    SOL    0      0      f0100184 2985   ./inc/x86.h
180    SLINE  0      16     0000000d 0      
181    SOL    0      0      f010018a 2970   kern/console.c
182    SLINE  0      55     00000013 0      
183    SLINE  0      54     00000018 0      
184    SLINE  0      56     0000001d 0 
```

inb函数是在./inc/x86.h中定义的，注意.表示上一级目录，也就是lab/inc/x86.h。COM1、COM_LSR是在kern/console.c中定义的。

N_FUN表示函数名，比如kern/init.c中有函数：

```
68     SO     0      2      f0100040 2712   kern/init.c
98     FUN    0      0      f0100040 2796   test_backtrace:F(0,20)
108    FUN    0      0      f0100094 2837   i386_init:F(0,20)
115    FUN    0      0      f01000e6 2855   _panic:F(0,20)
130    FUN    0      0      f010013d 2926   _warn:F(0,20)
```

截取片段：

```
108    FUN    0      0      f0100094 2837   i386_init:F(0,20)
109    SLINE  0      24     00000000 0      
110    SLINE  0      30     00000006 0      
111    SLINE  0      34     0000001d 0      
112    SLINE  0      36     00000022 0      
113    SLINE  0      39     00000034 0      
114    SLINE  0      43     00000043 0
```

这个片段是什么意思呢？首先要理解第一行给出的每列字段的含义：

- n_strx是符号索引，换句话说，整个符号表看作一个数组，n_strx是当前符号在数组中的下标；
- n_type是符号类型，FUN指函数名，SLINE指在text段中的行号；
-  n_othr目前没被使用，其值固定为0；
- n_value表示地址。特别要注意的是，这里只有FUN类型的符号的地址是绝对地址，SLINE符号的地址是偏移量，其实际地址为函数地址加上偏移量。比如第3行的含义是地址f010009a(=0xf0100094+0x00000006)对应文件第30行memset函数。

```c
 22 void
 23 i386_init(void)
 24 {
 25     extern char edata[], end[];
 26 
 27     // Before doing anything else, complete the ELF loading process.
 28     // Clear the uninitialized global data (BSS) section of our program.
 29     // This ensures that all static/global variables start out zero.
 30     memset(edata, 0, end - edata);
 31 
 32     // Initialize the console.
 33     // Can't call cprintf until after we do this!
 34     cons_init();
 35 
 36     cprintf("6828 decimal is %o octal!\n", 6828);
 37 
 38     // Test the stack backtrace function (lab 1 only)
 39     test_backtrace(5);
 40 
 41     // Drop into the kernel monitor.
 42     while (1)
 43         monitor(NULL);
 44 }
```

```
 118 void
 119 i386_init(void)
 120 {
 121 f0100094:   55                      push   %ebp
 122 f0100095:   89 e5                   mov    %esp,%ebp
 123 f0100097:   83 ec 0c                sub    $0xc,%esp
 124     extern char edata[], end[];
 125 
 126     // Before doing anything else, complete the ELF loading process.
 127     // Clear the uninitialized global data (BSS) section of our program.
 128     // This ensures that all static/global variables start out zero.
 129     memset(edata, 0, end - edata);
 130 f010009a:   b8 40 29 11 f0          mov    $0xf0112940,%eax
 131 f010009f:   2d 00 23 11 f0          sub    $0xf0112300,%eax
 132 f01000a4:   50                      push   %eax
 133 f01000a5:   6a 00                   push   $0x0
 134 f01000a7:   68 00 23 11 f0          push   $0xf0112300
 135 f01000ac:   e8 f3 12 00 00          call   f01013a4 <memset>
 136 
 137     // Initialize the console.
 138     // Can't call cprintf until after we do this!
 139     cons_init();
 140 f01000b1:   e8 9d 04 00 00          call   f0100553 <cons_init>
 141 
 142     cprintf("6828 decimal is %o octal!\n", 6828);
 143 f01000b6:   83 c4 08                add    $0x8,%esp
 144 f01000b9:   68 ac 1a 00 00          push   $0x1aac
 145 f01000be:   68 77 18 10 f0          push   $0xf0101877
 146 f01000c3:   e8 72 08 00 00          call   f010093a <cprintf>
```

4、 在lab目录下执行```gcc -pipe -nostdinc -O2 -fno-builtin -I. -MD -Wall -Wno-format -DJOS_KERNEL -gstabs -c -S kern/init.c```，然后找到并查看```init.s```文件。

```
  4 .Ltext0:
  5     .stabs  "gcc2_compiled.",60,0,0,0
  6     .stabs  "int:t(0,1)=r(0,1);-2147483648;2147483647;",128,0,0,0
  7     .stabs  "char:t(0,2)=r(0,2);0;127;",128,0,0,0
  8     .stabs  "long int:t(0,3)=r(0,3);-2147483648;2147483647;",128,0,0,0
  9     .stabs  "unsigned int:t(0,4)=r(0,4);0;4294967295;",128,0,0,0
 10     .stabs  "long unsigned int:t(0,5)=r(0,5);0;4294967295;",128,0,0,
.... 
 31     .stabs  "./inc/string.h",130,0,0,0
 32     .stabs  "./inc/types.h",130,0,0,0
 33     .stabs  "bool:t(4,1)=(4,2)=eFalse:0,True:1,;",128,0,0,0
 34     .stabs  " :T(4,3)=efalse:0,true:1,;",128,0,0,0
 35     .stabs  "int8_t:t(4,4)=(0,12)",128,0,0,0
 36     .stabs  "uint8_t:t(4,5)=(0,13)",128,0,0,0
 37     .stabs  "int16_t:t(4,6)=(0,10)",128,0,0,0
 38     .stabs  "uint16_t:t(4,7)=(0,11)",128,0,0,0
 39     .stabs  "int32_t:t(4,8)=(0,1)",128,0,0,0
 40     .stabs  "uint32_t:t(4,9)=(0,4)",128,0,0,0
 41     .stabs  "int64_t:t(4,10)=(0,8)",128,0,0,0
 42     .stabs  "uint64_t:t(4,11)=(0,9)",128,0,0,0
 43     .stabs  "intptr_t:t(4,12)=(4,8)",128,0,0,0
 44     .stabs  "uintptr_t:t(4,13)=(4,9)",128,0,0,0
 45     .stabs  "physaddr_t:t(4,14)=(4,9)",128,0,0,0
 46     .stabs  "ppn_t:t(4,15)=(4,9)",128,0,0,0
 47     .stabs  "size_t:t(4,16)=(4,9)",128,0,0,0
 48     .stabs  "ssize_t:t(4,17)=(4,8)",128,0,0,0
 49     .stabs  "off_t:t(4,18)=(4,8)",128,0,0,0
 50     .stabn  162,0,0,0
 51     .stabn  162,0,0,0
 52     .section    .rodata.str1.1,"aMS",@progbits,1
 53 .LC0:
 54     .string "entering test_backtrace %d\n"
 55 .LC1:
 56     .string "leaving test_backtrace %d\n"
 57     .section    .text.unlikely,"ax",@progbits
```

5、确认boot loader在加载内核时是否把符号表也加载到内存中。怎么确认呢？使用gdb查看符号表的位置是否存储有符号信息就知道啦首先，根据第3步的输出结果我们知道.stabstr段的加载内存地址为f01058c5，使用x/16s 0xf01058c5打印前16个字符串信息，结果如下所示。可见加载内核时符号表也一起加载到内存中了。

```
(gdb) b i386_init
(gdb) c
(gdb) x/16s 0xf01058c5
0xf01058c5:	""
0xf01058c6:	"{standard input}"
0xf01058d7:	"kern/entry.S"
0xf01058e4:	"kern/entrypgdir.c"
0xf01058f6:	"gcc2_compiled."
0xf0105905:	"int:t(0,1)=r(0,1);-2147483648;2147483647;"
0xf010592f:	"char:t(0,2)=r(0,2);0;127;"
0xf0105949:	"long int:t(0,3)=r(0,3);-2147483648;2147483647;"
0xf0105978:	"unsigned int:t(0,4)=r(0,4);0;4294967295;"
0xf01059a1:	"long unsigned int:t(0,5)=r(0,5);0;4294967295;"
0xf01059cf:	"__int128:t(0,6)=r(0,6);0;-1;"
0xf01059ec:	"__int128 unsigned:t(0,7)=r(0,7);0;-1;"
0xf0105a12:	"long long int:t(0,8)=r(0,8);-0;4294967295;"
0xf0105a3d:	"long long unsigned int:t(0,9)=r(0,9);0;-1;"
0xf0105a68:	"short int:t(0,10)=r(0,10);-32768;32767;"
0xf0105a90:	"short unsigned int:t(0,11)=r(0,11);0;65535;"
```

与init.s中的stabs信息一致。

6、编写代码实现eip，函数名，源文件名，与eip对应的行号的显示。

通过调用```stab_binsearch```来查找一个地址的行号，完成```debuginfo_eip```的实现。

```stab_binsearch(stabs, region_left, region_right, type, addr)```

stabs是stab结构的数组，region_left表示数组下标0，region_right表示数组的右下标，type表示类型，addr表示虚拟地址。

```
//stab按照指令地址升序排列，type类型为N_FUN的stabs表示函数，如前图中的i386_init函数，N_SO表示源文件，比如kern/entrypgdir.c的类型就是N_SO。给定一个指令地址，这个函数找到包含指令地址并且类型为type的表项。
13  SO    0xf0100040   kern/entrypgdir.c
68  SO    0xf0100040   kern/init.c
142  SO   0xf0100177   kern/console.c
396  SO   0xf0100686   kern/monitor.c
556  SO   0xf010094a   kern/kdebug.c
684  SO   0xf0100c11   lib/printfmt.c
```

```c
 36 //  For example, given these N_SO stabs:
 37 //      Index  Type   Address
 38 //      0      SO     f0100000
 39 //      13     SO     f0100040
 40 //      117    SO     f0100176
 41 //      118    SO     f0100178
 42 //      555    SO     f0100652
 43 //      556    SO     f0100654
 44 //      657    SO     f0100849
 45 //  this code:
 46 //      left = 0, right = 657;
 47 //      stab_binsearch(stabs, &left, &right, N_SO, 0xf0100184);
 48 //  will exit setting left = 118, right = 554.注意是555-1
 50 static void
 51 stab_binsearch(const struct Stab *stabs, int *region_left, int *region_right,
 52            int type, uintptr_t addr)
 53 {
 54     int l = *region_left, r = *region_right, any_matches = 0;
 55 
 56     while (l <= r) {
 57         int true_m = (l + r) / 2, m = true_m;
 58 
 59         // search for earliest stab with right type
 60         while (m >= l && stabs[m].n_type != type)
 61             m--;
 62         if (m < l) {    // no match in [l, m]
 63             l = true_m + 1;
 64             continue;
 65         }
 66 
 67         // actual binary search
 68         any_matches = 1;
 69         if (stabs[m].n_value < addr) { //如果当前地址小于目标地址
 70             *region_left = m;
 71             l = true_m + 1;
 72         } else if (stabs[m].n_value > addr) {//如果当前地址大于目标地址
 73             *region_right = m - 1;
 74             r = m - 1;
 75         } else {   
 76             // exact match for 'addr', but continue loop to find
 77             // *region_right  找到了目标地址之后，需要找*region_right的值
 78             *region_left = m;
 79             l = m;
 80             addr++;
 81         }
 82     }
 83 
 84     if (!any_matches)
 85         *region_right = *region_left - 1;
 86     else {
 87         // find rightmost region containing 'addr'
 88         for (l = *region_right;
 89              l > *region_left && stabs[l].n_type != type;
 90              l--)
 91             /* do nothing */;
 92         *region_left = l;
 93     }
 94 }    
```

```stab_binsearch```执行完结果left指向addr地址或者地址前最近一个stab，right指向下一个type的stab前一个。

​		在```debuginfo_eip```函数中调用了```stab_binsearch```，```int debuginfo_eip(uintptr_t addr, struct Eipdebuginfo *info)```函数将指令地址的信息填充到```Eipdebuginfo```结构中。如果```addr```的信息找到了，函数返回0，如果没找到，则返回负值，但是返回负值，仍然存储了一些信息给info。

```c
  6 // Debug information about a particular instruction pointer
  7 struct Eipdebuginfo {
  8     const char *eip_file;       // Source code filename for EIP
  9     int eip_line;           // Source code linenumber for EIP
 10 
 11     const char *eip_fn_name;    // Name of function containing EIP
 12                     //  - Note: not null terminated!
 13     int eip_fn_namelen;     // Length of function name
 14     uintptr_t eip_fn_addr;      // Address of start of function
 15     int eip_fn_narg;        // Number of function arguments
 16 };
//const char *eip_file;      源代码文件名
//int eip_line;              源代码行号
//const char *eip_fn_name;   包含eip的函数名
//int eip_fn_namelen;        函数名长度
//uintptr_t eip_fn_addr;     函数起始地址
//int eip_fn_narg;           函数参数个数
```

1、首先找到包含eip的源文件，也就是定义l位于eip的函数的源文件，类型为N_SO。

```c
134     // Now we find the right stabs that define the function containing
135     // 'eip'.  First, we find the basic source file containing 'eip'.
136     // Then, we look in that source file for the function.  Then we look
137     // for the line number.
138 
139     // Search the entire set of stabs for the source file (type N_SO).
140     lfile = 0;
141     rfile = (stab_end - stabs) - 1;
142     stab_binsearch(stabs, &lfile, &rfile, N_SO, addr);
143     if (lfile == 0)
144         return -1;
```

如果lfile等于0，没找到，返回负值。

2、在源文件的索引范围(lfile, rfile)，接着在源文件中找地址为eip的函数。

```c
146     // Search within that file's stabs for the function definition
147     // (N_FUN).
148     lfun = lfile;
149     rfun = rfile;
150     stab_binsearch(stabs, &lfun, &rfun, N_FUN, addr);
151 
152     if (lfun <= rfun) {
153         // stabs[lfun] points to the function name
154         // in the string table, but check bounds just in case.
155         if (stabs[lfun].n_strx < stabstr_end - stabstr)
156             info->eip_fn_name = stabstr + stabs[lfun].n_strx;
157         info->eip_fn_addr = stabs[lfun].n_value;
158         addr -= info->eip_fn_addr;
159         // Search within the function definition for the line number.
160         lline = lfun;
161         rline = rfun;
162     } else {
163         // Couldn't find function stab!  Maybe we're in an assembly
164         // file.  Search the whole file for the line number.
165         info->eip_fn_addr = addr;
166         lline = lfile;
167         rline = rfile;
168     }
169     // Ignore stuff after the colon.
170     info->eip_fn_namelen = strfind(info->eip_fn_name, ':') - info->eip_fn_name;
```

​        如果找到了包含```eip```的函数，```stabs[lfun]```对应于函数名，```stabs[lfun].n_strx```为字符串表的索引，必须在字符串表起始地址```stabstr```与末尾地址```stabstr_end```之间，将函数名赋值```info->eip_fn_name```中。```info->eip_fn_name = stabstr+stabs[lfun].n_strx```。

```stabs[lfun].n_value```为函数地址，将其赋值给```info->eip_fn_addr```。

为在函数中找到对应行号，需要addr减去```info->eip_fn_addr```，再在函数stabs范围中查找行号，```lline = lfun; rline = rfun```.

如果没找到包含eip的函数stab，可能eip处于汇编语言中，那么需要搜索整个文件查找行号。```lline = lfile; rline = rfile```.

函数长度```info->eip_fn_namelen```等于:之前的长度，通过```strfind(info->eip_fn_name,’:’)```找出:的地址，减去```info->eip_fn_name```地址，得出函数长度。

```c
stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
if (lline > rline)
    return -1;
info->eip_line = stabs[lline].n_desc;
```

addr为函数地址的相对值。

```c
58 int
 59 mon_backtrace(int argc, char **argv, struct Trapframe *tf)
 60 {
 61     uint32_t ebp, *ptr_ebp;
 62     struct Eipdebuginfo info;
 63     int result;
 64 
 65     ebp = read_ebp();  //read current ebp value
 66     cprintf("Stack backtrace:\n");
 67 
 68     while(ebp != 0){
 69         ptr_ebp = (uint32_t *)ebp;  //transform to a pointer
 70         cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
 71             ebp, ptr_ebp[1], ptr_ebp[2], ptr_ebp[3], ptr_ebp[4], ptr_ebp[5], ptr_ebp[6]);
 72 
 73         memset(&info, 0, sizeof(struct Eipdebuginfo));
 74 
 75         result = debuginfo_eip(ptr_ebp[1], &info);
 76         if(result != 0){
 77             cprintf("failed to get debuginfo for eip %x.\r\n", ptr_ebp[1]);
 78         }
 79         else{
 80             cprintf("\t%s:%d: %.*s+%u\r\n", info.eip_file, info.eip_line,
 81                 info.eip_fn_namelen, info.eip_fn_name, ptr_ebp[1]-info.eip_fn_addr);
 82         }
 83 
 84         ebp = *ptr_ebp;
 85 
 86     }
 87 
 88     return 0;
 89 }
```

在lab目录下make grade.

```
running JOS: (1.2s) 
  printf: OK 
  backtrace count: OK 
  backtrace arguments: OK 
  backtrace symbols: OK 
  backtrace lines: OK 
Score: 50/50
```

```c
char *s = "this is test example";
printf("%.*s", 10, s);  //这里的常量10就是给*号的,你也可以给他一个变量来控制宽度
```

