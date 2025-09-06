// monitor.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <ncurses.h>

volatile sig_atomic_t stop = 0;
void handle_sigint(int sig){ (void)sig; stop = 1; }

typedef struct {
    unsigned long long user,nice,system,idle,iowait,irq,softirq,steal,guest,guest_nice;
} cpu_times_t;

typedef struct {
    int logical_cpus;
    int physical_cores;
    char model_name[256];
} cpuinfo_t;

typedef struct {
    unsigned long long total, free, available, buffers, cached;
    unsigned long long swap_total, swap_free;
} meminfo_t;

// --- leer /proc/stat (cpu total y por core) ---
int read_proc_stat(cpu_times_t *total, cpu_times_t *cores, int *num_cores) {
    FILE *f = fopen("/proc/stat","r");
    if(!f) return -1;
    char line[512];
    int core_count = 0;
    while(fgets(line,sizeof line,f)) {
        if(strncmp(line,"cpu ",4)==0) {
            sscanf(line,"cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &total->user,&total->nice,&total->system,&total->idle,
                   &total->iowait,&total->irq,&total->softirq,&total->steal,
                   &total->guest,&total->guest_nice);
        } else if(strncmp(line,"cpu",3)==0) {
            if(core_count < *num_cores) {
                sscanf(line,"cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &core_count,
                       &cores[core_count].user,&cores[core_count].nice,
                       &cores[core_count].system,&cores[core_count].idle,
                       &cores[core_count].iowait,&cores[core_count].irq,
                       &cores[core_count].softirq,&cores[core_count].steal,
                       &cores[core_count].guest,&cores[core_count].guest_nice);
            }
        }
    }
    fclose(f);
    return 0;
}

double compute_cpu_usage(const cpu_times_t *prev, const cpu_times_t *curr) {
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long curr_idle = curr->idle + curr->iowait;
    unsigned long long prev_total = prev->user+prev->nice+prev->system+prev->idle+
                                    prev->iowait+prev->irq+prev->softirq+prev->steal+
                                    prev->guest+prev->guest_nice;
    unsigned long long curr_total = curr->user+curr->nice+curr->system+curr->idle+
                                    curr->iowait+curr->irq+curr->softirq+curr->steal+
                                    curr->guest+curr->guest_nice;
    unsigned long long totald = curr_total - prev_total;
    unsigned long long idled = curr_idle - prev_idle;
    if(totald == 0) return 0.0;
    return (double)(totald - idled) * 100.0 / (double)totald;
}

// --- leer /proc/meminfo ---
int read_proc_meminfo(meminfo_t *m) {
    FILE *f = fopen("/proc/meminfo","r");
    if(!f) return -1;
    char line[256];
    m->total = m->free = m->available = m->buffers = m->cached = 0;
    m->swap_total = m->swap_free = 0;
    while(fgets(line,sizeof line,f)) {
        unsigned long long val;
        if(sscanf(line,"MemTotal: %llu kB",&val)==1) m->total=val;
        else if(sscanf(line,"MemFree: %llu kB",&val)==1) m->free=val;
        else if(sscanf(line,"MemAvailable: %llu kB",&val)==1) m->available=val;
        else if(sscanf(line,"Buffers: %llu kB",&val)==1) m->buffers=val;
        else if(sscanf(line,"Cached: %llu kB",&val)==1) m->cached=val;
        else if(sscanf(line,"SwapTotal: %llu kB",&val)==1) m->swap_total=val;
        else if(sscanf(line,"SwapFree: %llu kB",&val)==1) m->swap_free=val;
    }
    fclose(f);
    return 0;
}

// --- leer /proc/cpuinfo ---
int read_proc_cpuinfo(cpuinfo_t *ci) {
    FILE *f = fopen("/proc/cpuinfo","r");
    if(!f) return -1;
    char line[512];
    ci->logical_cpus = 0;
    ci->physical_cores = 0;
    ci->model_name[0] = '\0';
    while(fgets(line,sizeof line,f)) {
        if(strncmp(line,"processor",9)==0) ci->logical_cpus++;
        else if(strncmp(line,"cpu cores",9)==0 && ci->physical_cores==0) {
            sscanf(line,"cpu cores\t: %d",&ci->physical_cores);
        } else if(strncmp(line,"model name",10)==0 && ci->model_name[0]=='\0') {
            char *p=strchr(line,':');
            if(p){ p++; while(*p==' '||*p=='\t') p++;
                strncpy(ci->model_name,p,sizeof(ci->model_name)-1);
                ci->model_name[sizeof(ci->model_name)-1]='\0';
                char *nl=strchr(ci->model_name,'\n'); if(nl) *nl='\0';
            }
        }
    }
    fclose(f);
    return 0;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    cpuinfo_t ci;
    if(read_proc_cpuinfo(&ci)!=0){
        fprintf(stderr,"No se pudo leer /proc/cpuinfo\n");
        return 1;
    }

    int cores_count = ci.logical_cpus;
    cpu_times_t prev_total, curr_total;
    cpu_times_t *prev_cores = calloc(cores_count,sizeof(cpu_times_t));
    cpu_times_t *curr_cores = calloc(cores_count,sizeof(cpu_times_t));

    read_proc_stat(&prev_total,prev_cores,&cores_count);

    // --- inicializar ncurses ---
    initscr();
    noecho();
    curs_set(FALSE);

    while(!stop) {
        sleep(2);
        read_proc_stat(&curr_total,curr_cores,&cores_count);
        double cpu_pct = compute_cpu_usage(&prev_total,&curr_total);

        meminfo_t m;
        read_proc_meminfo(&m);

        unsigned long long used_kb = (m.available? m.total - m.available :
                                      m.total - m.free - m.buffers - m.cached);
        double total_gb = (double)m.total/1024.0/1024.0;
        double used_gb = (double)used_kb/1024.0/1024.0;
        double used_pct = (double)used_kb*100.0/(double)m.total;

        unsigned long long swap_used = m.swap_total - m.swap_free;
        double swap_gb = (double)swap_used/1024.0/1024.0;

        erase();
        mvprintw(0,0,"CPU: %s", ci.model_name);
        mvprintw(1,0,"Procesadores logicos: %d | Cores fisicos: %d",
                 ci.logical_cpus, ci.physical_cores);
        mvprintw(3,0,"Uso CPU total: %.2f%%", cpu_pct);

        for(int i=0;i<cores_count;i++){
            double core_pct = compute_cpu_usage(&prev_cores[i],&curr_cores[i]);
            mvprintw(4+i,0,"Core %d: %.2f%%", i, core_pct);
            prev_cores[i]=curr_cores[i];
        }

        mvprintw(4+cores_count,0,"RAM total: %.2f GB | RAM usada: %.2f GB (%.1f%%)",
                 total_gb, used_gb, used_pct);
        mvprintw(5+cores_count,0,"Swap usada: %.2f GB", swap_gb);

        refresh();
        prev_total=curr_total;
    }

    endwin();
    free(prev_cores);
    free(curr_cores);
    printf("Saliendo...\n");
    return 0;
}
