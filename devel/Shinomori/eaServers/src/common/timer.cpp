// $Id: timer.c,v 1.1.1.1 2004/09/10 17:44:49 MagicalTux Exp $
// original : core.c 2003/02/26 18:03:12 Rev 1.7

#include "timer.h"
#include "utils.h"
#include "malloc.h"
#include "showmsg.h"


// タイマー間隔の最小値。モンスターの大量召還時、多数のクライアント接続時に
// サーバーが反応しなくなる場合は、TIMER_MIN_INTERVAL を増やしてください。

// If the server shows no reaction when processing thousands of monsters
// or connected by many clients, please increase TIMER_MIN_INTERVAL.

#define TIMER_MIN_INTERVAL 50

static struct TimerData*	timer_data		=NULL;
static size_t				timer_data_max	=0;
static size_t				timer_data_pos	=0;

static size_t*				timer_free		=NULL;
static size_t				timer_free_max	=0;
static size_t				timer_free_pos	=0;

static size_t* 				timer_heap		=NULL;
static size_t				timer_heap_max	=0;
static size_t				timer_heap_pos	=0;


/////////////////////////////////////////////////////////////////////////////////////
// for debug
struct timer_func_list
{
	int (*func)(int,unsigned long,int,int);
	struct timer_func_list* next;
	char name[2];// will allocate larger from here and copy the name beyond this structure
};

static struct timer_func_list* tfl_root=NULL;

//
int add_timer_func_list(int (*func)(int,unsigned long,int,int),char* name)
{
	if(name)
	{	// pad a struct timer_func_list together with the name
		struct timer_func_list* tfl = (struct timer_func_list*)aMalloc(sizeof(struct timer_func_list) + strlen(name) );

		tfl->func = func;
		memcpy(tfl->name, name, 1+strlen(name));
		tfl->next = tfl_root;
		tfl_root = tfl;
	}
	return 0;
}

char* search_timer_func_list(int (*func)(int,unsigned long,int,int))
{
	struct timer_func_list* tfl;
	for(tfl = tfl_root;tfl;tfl = tfl->next) {
		if (func == tfl->func)
			return tfl->name;
	}
	return "unknown timer function";
}
/////////////////////////////////////////////////////////////////////////////////////
/*----------------------------
 * 	Get tick time
 *----------------------------*/
/*
static unsigned long gettick_cache;
static int gettick_count;

unsigned long gettick_nocache(void)
{
	gettick_count = 256;
	return gettick_cache = GetTickCount();
}

unsigned long gettick(void)
{
	gettick_count--;
	if (gettick_count<0)
		return gettick_nocache();
	return gettick_cache;
}
*/

/////////////////////////////////////////////////////////////////////////////////////
/*======================================
 * 	CORE : Timer Heap
 *--------------------------------------
 */

inline int timer_data_diff_tick(size_t a, size_t b)
{
	// sort for tick count first
	unsigned long x = timer_data[a].tick - timer_data[b].tick;
	// then sort for index number
	if( x==0 ) return a-b;
	unsigned long y = timer_data[b].tick - timer_data[a].tick;
	return (x<y) ? ((int)x) : -((int)y);
}


int search_timer_heap(size_t* field, size_t count, size_t elem, size_t &pos)
{	// do a binary search
	// descending sort (largest at 0, smallest at count-1
	// make some initial stuff
	// and test the boundaries first
	int ret = false;
	int cmp;
	size_t a=0;
	size_t b=count-1;	
	size_t c;
	pos = 0;

	if( NULL==field || 0==count)
		ret = false;
	else if( 0 < (cmp = timer_data_diff_tick(elem, timer_heap[a])) )
	{	pos = a;
		ret = false; 
	}
	else if( 0 == cmp ) 
	{	pos=a;
		ret = true;
	}
	else if( 0 > (cmp = timer_data_diff_tick(elem, timer_heap[b])) )
	{	pos = b+1;
		ret = false; 
	}
	else if( 0 == cmp ) 
	{	pos=b;
		ret = true;
	}
	else
	{	// binary search
		do
		{
			c=(a+b)/2;
			if( 0 == (cmp = timer_data_diff_tick(elem, timer_heap[c])) )
			{	b=c;
				ret = true;
				break;
			}
			else if( 0 < cmp )
				b=c;
			else
				a=c;
		}while( (a+1) < b );
		pos = b;
	}
	return ret;
}


// デバッグ用関数群
void dump_timer_heap(void) {
	size_t j;
	for(j = 0; j < timer_heap_pos; j++) {
		if( j != (timer_heap_pos-1) && 
			timer_data_diff_tick( timer_heap[j], timer_heap[j+1]) < 0) {
			printf("*");
		} else {
			printf(" ");
		}
		printf("%4d : %4d %ld\n", j, timer_heap[j], timer_data[timer_heap[j]].tick);
	}
}

int push_timer_heap(size_t index)
{
	size_t pos;
	// check for sufficient buffer size

	if ( timer_heap_pos >= timer_heap_max )
	{	// will need proper initialisation before coming here
		timer_heap_max += 256;
		timer_heap = (size_t*)aRealloc(timer_heap, timer_heap_max*sizeof(size_t));
		memset(timer_heap + (timer_heap_max - 256), 0, 256*sizeof(size_t));
	}

	// push value to the heap
	if( !search_timer_heap(timer_heap, timer_heap_pos, index, pos) )
	{
		// move all elements after pos for one step
		memmove( timer_heap+pos+1, timer_heap+pos, (timer_heap_pos-pos)*sizeof(size_t) );
		timer_heap[pos] = index;
		timer_heap_pos++;
	}
	//	else we found this one already in the heap and do not re-insert
	return 0;
}

void delete_timer_heap(size_t index) 
{
	size_t pos;
	if( search_timer_heap(timer_heap, timer_heap_pos, index, pos) )
	{
		memmove(timer_heap+pos,timer_heap+pos+1,(timer_heap_pos - pos) * sizeof(size_t));
		timer_heap_pos--;
	}
	//	else we did not found it and don't have to do anything
}

inline int top_timer_heap(void)
{
	// get the first element and keep it on the heap
	if (timer_heap == NULL || timer_heap_pos <= 0)
		return -1;
	return timer_heap[timer_heap_pos-1];
}

inline int pop_timer_heap(void)
{
	// get the first element and remove it from the heap
	if (timer_heap == NULL || timer_heap_pos <= 0)
		return -1;
	timer_heap_pos--;
	return timer_heap[timer_heap_pos];
}


/*======================================
 *	CORE : Timer Heap 
 *	old method
 *--------------------------------------
 */
/*
void push_timer_heap(int index)
{
	size_t i,h;
	// need proper initialisation before comming here
	if(timer_heap_pos >= timer_heap_max){
		timer_heap_max += 256;
		timer_heap = (size_t*)aRealloc(timer_heap, timer_heap_max*sizeof(size_t));
				}

	timer_heap_pos++;

	for (h = timer_heap_pos-1, i = (h-1)/2;
		(h > 0) && (0 > DIFF_TICK(timer_data[index].tick , timer_data[timer_heap[i+1]].tick));
		i = (h-1)/2) {
		timer_heap[h] = timer_heap[i];
		h = i;
			}
	timer_heap[h]=index;
				}


inline int top_timer_heap()
{
	if(timer_heap==NULL || timer_heap_pos<=0)
		return -1;
	return timer_heap[0];
			}



inline int pop_timer_heap()
{
	int i,h;
	size_t k;
	size_t ret, last;

	if(timer_heap==NULL || timer_heap_pos<=0)
			return -1;

	ret	= timer_heap[0];
	last= timer_heap[timer_heap_pos-1];

	timer_heap_pos--;

	for(h=0, k=2; k<timer_heap_pos; k=k*2+2){
		if(0 < DIFF_TICK(timer_data[timer_heap[k]].tick , timer_data[timer_heap[k-1]].tick) )
			k--;
		timer_heap[h]=timer_heap[k];
		h = k;
		}
	if(k == timer_heap_pos) {
		timer_heap[h]=timer_heap[k-1], h = k-1;
	}

	for(i = (h-1)/2;
		(h > 0) && (0 < DIFF_TICK(timer_data[timer_heap[i+1]].tick , timer_data[last].tick));
		i = (h-1)/2) {
		timer_heap[h]=timer_heap[i], h = i;
}
	timer_heap[h]=last;

	return ret;
	}

*/


/////////////////////////////////////////////////////////////////////////////////////
/*======================================
 * 	Timer Management
 *--------------------------------------
 */

size_t aquire_timer(void)
{
	size_t ret;
	// get a timer from free_timer if possible
	if( timer_free_pos > 0 ) {
		timer_free_pos--;
		ret = timer_free[timer_free_pos];
	}
	// check if there are unused timers at the end of timer_data
	else {
		if( timer_data_pos >= timer_data_max) {
			// no, we have to enlarge timer_data
			// will need proper initialisation before coming here
			timer_data_max += 256;
			timer_data = (struct TimerData*)aRealloc(timer_data, timer_data_max*sizeof(struct TimerData));
			memset(timer_data + (timer_data_max - 256), 0, 256*sizeof(struct TimerData));
		}
		ret = timer_data_pos;
		timer_data_pos++;
	}
	// a valid and free index for timer_data
	return ret;
}


void release_timer(size_t tid)
{
	// clean the timer before putting it to the free timers
	if( (timer_data[tid].type.pt) && (0 != timer_data[tid].data) )
	{	// clear if pointer still exist
		void *p = (void*)timer_data[tid].data;
		aFree(p);
		timer_data[tid].data	= 0;
	}
	timer_data[tid].type.pt  = false;
	timer_data[tid].func     = NULL;
	timer_data[tid].interval = 0;
	timer_data[tid].tick     = 0;


	// check if there are free space at timer_free
	if( timer_free_pos >= timer_free_max) {
		// no, we have to enlarge timer_free
		// will need proper initialisation before coming here
		timer_free_max += 256;
		timer_free = (size_t*)aRealloc(timer_free, timer_free_max*sizeof(size_t));
		memset(timer_free + (timer_free_max - 256), 0, 256*sizeof(size_t));
	}
	timer_free[timer_free_pos++] = tid;
}

/////////////////////////////////////////////////////////////////////////////////////
int add_timer(unsigned long tick, int (*func)(int,unsigned long,int,int),int id, int data)
{
	size_t tid = aquire_timer();
	
	timer_data[tid].tick	 = tick;
	timer_data[tid].func	 = func;
	timer_data[tid].id		 = id;
	timer_data[tid].data	 = data;
	timer_data[tid].type.pt  = false;
	timer_data[tid].interval = 0;
	
	push_timer_heap(tid);
	return tid;
}
int add_timer_p(unsigned long tick, int (*func)(int,unsigned long,int,int),int id, void* pdata)
{
	size_t tid = aquire_timer();
	
	timer_data[tid].tick	 = tick;
	timer_data[tid].func	 = func;
	timer_data[tid].id		 = id;
	timer_data[tid].data	 = (int)pdata;
	timer_data[tid].type.pt  = true;
	timer_data[tid].interval = 0;
	
	push_timer_heap(tid);
	return tid;
}
int add_timer_interval(unsigned long tick, unsigned long interval,int (*func)(int,unsigned long,int,int),int id,int data)
{
	size_t tid = aquire_timer();

	timer_data[tid].tick	 = tick;
	timer_data[tid].func	 = func;
	timer_data[tid].id		 = id;
	timer_data[tid].data	 = data;
	timer_data[tid].type.pt  = false;
	timer_data[tid].interval = interval;

	push_timer_heap(tid);
	return tid;
}
int add_timer_interval_p(unsigned long tick, unsigned long interval,int (*func)(int,unsigned long,int,int),int id,void*pdata)
{
	size_t tid = aquire_timer();

	timer_data[tid].tick	 = tick;
	timer_data[tid].func	 = func;
	timer_data[tid].id		 = id;
	timer_data[tid].data	 = (int)pdata;
	timer_data[tid].type.pt  = true;
	timer_data[tid].interval = interval;

	push_timer_heap(tid);
	return tid;
}

int delete_timer(size_t tid, int (*func)(int,unsigned long,int,int))
{
	// a basic check
	if( tid >= timer_data_pos ) {
		ShowMessage("delete_timer error : no such timer %d\n", tid);
		return -1;
	}
	// more specific check
	if( timer_data[tid].func != func ) {
		ShowMessage("delete_timer error : function dismatch %08x(%s) != %08x(%s)\n",
			(int)timer_data[tid].func, search_timer_func_list(timer_data[tid].func),
			(int)func, search_timer_func_list(func));
		return -2;
	}

	// そのうち消えるにまかせる
	timer_data[tid].func     = NULL;
	timer_data[tid].interval = 0;
	// changing the tick will confuse the binary search
//	timer_data[tid].tick -= 60 * 60 * 1000; 
	return 0;
}

int addtick_timer(size_t tid, unsigned long tick)
{
	delete_timer_heap(tid);
	timer_data[tid].tick += tick;
	push_timer_heap(tid);
	return timer_data[tid].tick;
}

struct TimerData* get_timer(size_t tid)
{
	return &timer_data[tid];
}


int do_timer(unsigned long tick)
{
	int tid, nextmin = 1000;

/*
// Timer Display, list status and first 5 ticks
size_t sz, end = (timer_heap_pos > 5) ? 5 : timer_heap_pos;
printf("timer %d (%d + %d): ",timer_data_pos,timer_heap_pos,timer_free_pos);
for(sz=0; sz<end; sz++)
	printf("%ld  ",timer_data[ timer_heap[timer_heap_pos-1-sz] ].tick);
printf("\r");
fflush(stdout);
*/

	while( timer_heap_pos > 0) {
		// there is something on the heap

		// get the first element
		tid = top_timer_heap();	

		// check if tick of timer is near
		nextmin = DIFF_TICK(timer_data[tid].tick , tick);
		if( nextmin > 0) {
			// need to wait
			break;
		}

		// wait time is over, we are processing the timer
		// so pop it from the stack
		pop_timer_heap();

		// call the timer function
		if( NULL != timer_data[tid].func ) {
			if (DIFF_TICK(timer_data[tid].tick , tick) < -1000) {
				// 1秒以上の大幅な遅延が発生しているので、
				// timer処理タイミングを現在値とする事で
				// 呼び出し時タイミング(引数のtick)相対で処理してる
				// timer関数の次回処理タイミングを遅らせる
				timer_data[tid].func(tid,tick,timer_data[tid].id,timer_data[tid].data);
			} else {
				timer_data[tid].func(tid,timer_data[tid].tick,timer_data[tid].id,timer_data[tid].data);
			}
		}

		if( timer_data[tid].interval>0 )
		{	// interval timer is reinserted
			if (DIFF_TICK(timer_data[tid].tick , tick) < -1000) {
				timer_data[tid].tick = tick + timer_data[tid].interval;
			} else {
				timer_data[tid].tick += timer_data[tid].interval;
			}
			push_timer_heap(tid);
		}
		else
		{	// timer_once is freed
			release_timer(tid);
		}
	}// end while

	if(nextmin < TIMER_MIN_INTERVAL)
		nextmin = TIMER_MIN_INTERVAL;
	return nextmin;
}


void timer_init(void) 
{	// nothing to do here
}

void timer_final(void) 
{
	struct timer_func_list *tfl_next;
	while(tfl_root)
	{
		tfl_next = tfl_root->next;
		aFree(tfl_root);
		tfl_root = tfl_next;
	}

	if(timer_heap)
	{
		aFree(timer_heap);
		timer_heap=NULL;
	}
	if(timer_free)
	{
		aFree(timer_free);
		timer_free=NULL;
	}
	if(timer_data)
	{
		aFree(timer_data);
		timer_data=NULL;
	}
}
