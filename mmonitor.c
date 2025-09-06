// monitor.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

volatile sig_atomic_t stop = 0;
void handle_sigint(int sig){ (void)sig; stop = 1; }

typedef struct {
    unsigned long long user,nice,system,idle,iowait,irq,softirq,steal,guest,guest_nice;
} cpu_times_t;

int read_proc_stat(cpu_times_t *ct) {
    FILE *f = fopen("/proc/stat","r");
    if(!f) return -1;
    char line[512];
    if(!fgets(line,sizeof line,f)) { fclose(f); return -1; }
    if(strncmp(line,"cpu ",4) != 0) {
        while(fgets(line,sizeof line,f)) {
            if(strncmp(line,"cpu ",4)==0) break;
        }
    }
    fclose(f);
    // tokenizar la línea después de "cpu"
    char *tok;
    char buf[512];
    strncpy(buf,line,sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    tok = strtok(buf," \t"); // "cpu"
    unsigned long long vals[10] = {0};
    int i = 0;
    while((tok = strtok(NULL," \t")) && i < 10) {
        if(*tok == '\0') continue;
        vals[i++] = strtoull(tok, NULL, 10);
    }
    ct->user = vals[0]; ct->nice = vals[1]; ct->system = vals[2];
    ct->idle = vals[3]; ct->iowait = vals[4]; ct->irq = vals[5];
    ct->softirq = vals[6]; ct->steal = vals[7]; ct->guest = vals[8];
    ct->guest_nice = vals[9];
    return 0;
}

double compute_cpu_usage(const cpu_times_t *prev, const cpu_times_t *curr) {
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long curr_idle = curr->idle + curr->iowait;
    unsigned long long prev_total = prev->user + prev->nice + prev->system + prev->idle + prev->iowait + prev->irq + prev->softirq + prev->steal + prev->guest + prev->guest_nice;
    unsigned long long curr_total = curr->user + curr->nice + curr->system + curr->idle + curr->iowait + curr->irq + curr->softirq + curr->steal + curr->guest + curr->guest_nice;
    unsigned long long totald = curr_total - prev_total;
    unsigned long long idled = curr_idle - prev_idle;
    if(totald == 0) return 0.0;
    return (double)(totald - idled) * 100.0 / (double)totald;
}

typedef struct {
    unsigned long long total, free, available, buffers, cached;
} meminfo_t;

int read_proc_meminfo(meminfo_t *m) {
    FILE *f = fopen("/proc/meminfo","r");
    if(!f) return -1;
    char line[256];
    m->total = m->free = m->available = m->buffers = m->cached = 0;
    while(fgets(line,sizeof line,f)) {
        unsigned long long val;
        if(sscanf(line,"MemTotal: %llu kB",&val) == 1) m->total = val;
        else if(sscanf(line,"MemFree: %llu kB",&val) == 1) m->free = val;
        else if(sscanf(line,"MemAvailable: %llu kB",&val) == 1) m->available = val;
        else if(sscanf(line,"Buffers: %llu kB",&val) == 1) m->buffers = val;
        else if(sscanf(line,"Cached: %llu kB",&val) == 1) m->cached = val;
    }
    fclose(f);
    return 0;
}

typedef struct {
    int logical_cpus;
    char model_name[256];
} cpuinfo_t;

int read_proc_cpuinfo(cpuinfo_t *ci) {
    FILE *f = fopen("/proc/cpuinfo","r");
    if(!f) return -1;
    char line[512];
    ci->logical_cpus = 0;
    ci->model_name[0] = '\0';
    while(fgets(line,sizeof line,f)) {
        if(strncmp(line,"processor",9) == 0) ci->logical_cpus++;
        else if(strncmp(line,"model name",10) == 0 && ci->model_name[0] == '\0') {
            char *p = strchr(line, ':');
            if(p) {
                p++;
                while(*p == ' ' || *p == '\t') p++;
                strncpy(ci->model_name, p, sizeof(ci->model_name)-1);
                ci->model_name[sizeof(ci->model_name)-1] = '\0';
                char *nl = strchr(ci->model_name, '\n');
                if(nl) *nl = '\0';
            }
        }
    }
    fclose(f);
    return 0;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    cpuinfo_t ci;
    if(read_proc_cpuinfo(&ci) == 0) {
        printf("CPU: %s\n", ci.model_name[0] ? ci.model_name : "Desconocido");
        printf("Procesadores lógicos: %d\n", ci.logical_cpus);
    } else {
        fprintf(stderr, "No se pudo leer /proc/cpuinfo\n");
    }

    cpu_times_t prev, curr;
    if(read_proc_stat(&prev) != 0) {
        fprintf(stderr, "No se pudo leer /proc/stat\n");
        return 1;
    }

    printf("Monitoreo (actualiza cada 2s). Presiona Ctrl+C para salir.\n");
    while(!stop) {
        sleep(2);
        if(read_proc_stat(&curr) != 0) {
            fprintf(stderr, "Error leyendo /proc/stat\n");
            break;
        }
        double cpu_pct = compute_cpu_usage(&prev, &curr);
        prev = curr;

        meminfo_t m;
        if(read_proc_meminfo(&m) != 0) {
            fprintf(stderr, "Error leyendo /proc/meminfo\n");
            break;
        }

        unsigned long long used_kb = 0;
        if(m.available) used_kb = m.total - m.available;
        else used_kb = m.total - m.free - m.buffers - m.cached;

        double total_gb = (double)m.total / 1024.0 / 1024.0;
        double used_gb  = (double)used_kb / 1024.0 / 1024.0;
        double used_pct = (m.total > 0) ? ((double)used_kb * 100.0 / (double)m.total) : 0.0;

        time_t t = time(NULL);
        char ts[64];
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("[%s] CPU uso: %.2f%% | RAM total: %.2f GB | RAM usada: %.2f GB (%.2f%%)\n",
               ts, cpu_pct, total_gb, used_gb, used_pct);
        fflush(stdout);
    }

    printf("\nSaliendo...\n");
    return 0;
}
