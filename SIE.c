// sistema_escolar_pro.c
// Sistema Escolar completo em C — Android/PC (Cxxdroid/gcc).
// Pasta automática Relatorios/ para banco, relatórios e logs.
// Autor: Whesley (13y) — portfólio.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p),0700)
#endif

// ========================= Limites =========================
#define MAX_ALUNOS   1000
#define MAX_NOME     64
#define MAX_TURMA    32
#define MAX_DISC     12
#define DATA_DIR     "Relatorios"
#define DB_PATH      "Relatorios/banco.csv"
#define CFG_PATH     "Relatorios/config.txt"
#define LOG_PATH     "Relatorios/audit.log"

// ========================= Estruturas ======================
typedef struct {
    char   nome[MAX_NOME];
    char   matricula[24];
    char   turma[MAX_TURMA];
    double notas[MAX_DISC];       // 0..nota_max
    double rec;                   // -1 sem recuperação
    int    ndisc;                 // em uso
    int    presencas;             // nº presenças
    int    aulas;                 // nº aulas dadas
    double media;                 // média ponderada
    double media_final;           // com recuperação
    int    aprovado;              // 1/0
} Aluno;

typedef struct {
    int    ndisc;                 // qtd disciplinas
    double nota_max;              // nota máxima por disciplina
    double corte;                 // média mínima para aprovar
    char   nomes_disc[MAX_DISC][32];
    double pesos[MAX_DISC];       // pesos relativos (qualquer escala; normalizo)
    char   admin_pass[32];        // senha simples de admin
} Config;

// ========================= Globais =========================
static Aluno  g_alunos[MAX_ALUNOS];
static int    g_n = 0;
static Config g_cfg = { .ndisc=4, .nota_max=10.0, .corte=6.0,
                        .admin_pass="admin" };
static int    g_dirty = 0;        // houve alterações não salvas

// backup p/ desfazer
static Aluno  g_prev_alunos[MAX_ALUNOS];
static int    g_prev_n = 0;
static int    g_prev_valid = 0;

// ========================= Utils IO ========================
static void strip_newline(char *s){
    size_t n=strlen(s);
    if(n && (s[n-1]=='\n'||s[n-1]=='\r')) s[n-1]='\0';
}
static void ler_linha(char *buf, size_t n){
    if(!fgets(buf,n,stdin)){ buf[0]='\0'; return; }
    strip_newline(buf);
}
static int ler_int(){
    char b[64]; ler_linha(b,sizeof(b)); return atoi(b);
}
static double ler_double(){
    char b[64]; ler_linha(b,sizeof(b)); return atof(b);
}
static void ts(char *buf,size_t n){
    time_t t=time(NULL); struct tm *lt=localtime(&t);
    strftime(buf,n,"%Y-%m-%d %H:%M:%S",lt);
}
static void ts_file(char *buf,size_t n){
    time_t t=time(NULL); struct tm *lt=localtime(&t);
    strftime(buf,n,"%Y-%m-%d_%H-%M-%S",lt);
}
static void safe_text(char *s){ for(char *p=s; *p; ++p) if(*p==';') *p=','; }
static void barra(double v,double vmax,int w,FILE *out){
    if(vmax<=0) vmax=10.0; if(v<0)v=0; if(v>vmax)v=vmax;
    int f=(int)round((v/vmax)*w); if(f<0)f=0; if(f>w)f=w;
    for(int i=0;i<f;i++) fputc('#',out);
    for(int i=f;i<w;i++) fputc('.',out);
}
static void audit(const char *acao){
    FILE *f=fopen(LOG_PATH,"a"); if(!f) return;
    char t[32]; ts(t,sizeof(t));
    fprintf(f,"[%s] %s\n", t, acao);
    fclose(f);
}

// ========================= Pasta/Arquivos ==================
static void ensure_dir(){
    struct stat st;
    if(stat(DATA_DIR,&st)==-1) MKDIR(DATA_DIR);
    // cria arquivos base se não existirem
    FILE *f = fopen(CFG_PATH,"r");
    if(!f){
        f=fopen(CFG_PATH,"w");
        if(f){
            // nomes padrão e pesos=1
            for(int i=0;i<g_cfg.ndisc;i++){
                snprintf(g_cfg.nomes_disc[i],32,"Disc_%d",i+1);
                g_cfg.pesos[i]=1.0;
            }
            fprintf(f,"ndisc=%d\nnota_max=%.2f\ncorte=%.2f\nadmin=%s\n",
                    g_cfg.ndisc,g_cfg.nota_max,g_cfg.corte,g_cfg.admin_pass);
            for(int i=0;i<g_cfg.ndisc;i++)
                fprintf(f,"disc%d=%s\n",i+1,g_cfg.nomes_disc[i]);
            for(int i=0;i<g_cfg.ndisc;i++)
                fprintf(f,"peso%d=%.3f\n",i+1,g_cfg.pesos[i]);
            fclose(f);
        }
    } else fclose(f);
}

static void salvar_cfg(){
    FILE *f=fopen(CFG_PATH,"w"); if(!f){ puts("Falha ao salvar config."); return; }
    fprintf(f,"ndisc=%d\nnota_max=%.2f\ncorte=%.2f\nadmin=%s\n",
            g_cfg.ndisc,g_cfg.nota_max,g_cfg.corte,g_cfg.admin_pass);
    for(int i=0;i<g_cfg.ndisc;i++)
        fprintf(f,"disc%d=%s\n",i+1,g_cfg.nomes_disc[i]);
    for(int i=0;i<g_cfg.ndisc;i++)
        fprintf(f,"peso%d=%.3f\n",i+1,g_cfg.pesos[i]);
    fclose(f);
    audit("Salvar configuracao");
}

static void carregar_cfg(){
    FILE *f=fopen(CFG_PATH,"r"); if(!f) return;
    char k[64], v[256];
    // defaults caso arquivo antigo
    for(int i=0;i<MAX_DISC;i++){
        if(i<g_cfg.ndisc){
            if(!g_cfg.nomes_disc[i][0]) snprintf(g_cfg.nomes_disc[i],32,"Disc_%d",i+1);
            if(g_cfg.pesos[i]==0) g_cfg.pesos[i]=1.0;
        } else { g_cfg.pesos[i]=1.0; g_cfg.nomes_disc[i][0]='\0'; }
    }
    while(fscanf(f,"%63[^=\n]=%255[^\n]\n",k,v)==2){
        if(strcmp(k,"ndisc")==0){ g_cfg.ndisc=atoi(v); if(g_cfg.ndisc<1) g_cfg.ndisc=1; if(g_cfg.ndisc>MAX_DISC) g_cfg.ndisc=MAX_DISC; }
        else if(strcmp(k,"nota_max")==0){ g_cfg.nota_max=atof(v); if(g_cfg.nota_max<=0) g_cfg.nota_max=10.0; }
        else if(strcmp(k,"corte")==0){ g_cfg.corte=atof(v); }
        else if(strcmp(k,"admin")==0){ strncpy(g_cfg.admin_pass,v,sizeof(g_cfg.admin_pass)-1); g_cfg.admin_pass[sizeof(g_cfg.admin_pass)-1]='\0'; }
        else if(strncmp(k,"disc",4)==0){
            int idx=atoi(k+4)-1; if(idx>=0 && idx<MAX_DISC){
                strncpy(g_cfg.nomes_disc[idx],v,31); g_cfg.nomes_disc[idx][31]='\0';
            }
        } else if(strncmp(k,"peso",4)==0){
            int idx=atoi(k+4)-1; if(idx>=0 && idx<MAX_DISC){
                g_cfg.pesos[idx]=atof(v); if(g_cfg.pesos[idx]<=0) g_cfg.pesos[idx]=1.0;
            }
        }
    }
    fclose(f);
}

// ========================= Cálculos ========================
static void normaliza_pesos(double out[MAX_DISC]){
    double s=0; for(int i=0;i<g_cfg.ndisc;i++) s+=g_cfg.pesos[i];
    if(s<=0) s=1;
    for(int i=0;i<g_cfg.ndisc;i++) out[i]=g_cfg.pesos[i]/s;
}
static void recalcula_um(Aluno *a){
    double w[MAX_DISC]; normaliza_pesos(w);
    double soma=0;
    for(int i=0;i<g_cfg.ndisc;i++) soma += a->notas[i]*w[i];
    a->ndisc = g_cfg.ndisc;
    a->media = soma;
    // regra de recuperação: melhora pela média com rec
    if(a->rec>=0) a->media_final = fmax(a->media, (a->media + a->rec)/2.0);
    else a->media_final = a->media;
    a->aprovado = (a->media_final + 1e-9 >= g_cfg.corte);
}
static void recalcula_todos(){ for(int i=0;i<g_n;i++) recalcula_um(&g_alunos[i]); }

// ========================= Banco (CSV) =====================
// Formato:
// # nd,nota_max,corte
// # disc1;disc2;...;discN
// # peso1;peso2;...;pesoN
// matricula;nome;turma;rec;presencas;aulas;nota1;...;notaN
static void salvar_db(){
    FILE *f=fopen(DB_PATH,"w"); if(!f){ puts("Falha ao salvar banco."); return; }
    fprintf(f,"#%d;%.3f;%.3f\n", g_cfg.ndisc, g_cfg.nota_max, g_cfg.corte);
    for(int i=0;i<g_cfg.ndisc;i++){ fprintf(f,"%s%s", g_cfg.nomes_disc[i], (i==g_cfg.ndisc-1)?"\n":";"); }
    for(int i=0;i<g_cfg.ndisc;i++){ fprintf(f,"%.6f%s", g_cfg.pesos[i], (i==g_cfg.ndisc-1)?"\n":";"); }
    fprintf(f,"#admin=%s\n", g_cfg.admin_pass);

    for(int i=0;i<g_n;i++){
        char nome_s[MAX_NOME]; strncpy(nome_s,g_alunos[i].nome,MAX_NOME-1); nome_s[MAX_NOME-1]='\0'; safe_text(nome_s);
        fprintf(f,"%s;%s;%s;%.2f;%d;%d", g_alunos[i].matricula, nome_s, g_alunos[i].turma,
                g_alunos[i].rec, g_alunos[i].presencas, g_alunos[i].aulas);
        for(int j=0;j<g_cfg.ndisc;j++) fprintf(f,";%.2f", g_alunos[i].notas[j]);
        fputc('\n',f);
    }
    fclose(f);
    g_dirty=0;
    audit("Salvar banco");
}
static void carregar_db(){
    FILE *f=fopen(DB_PATH,"r"); if(!f) return;
    char linha[2048];
    // header 1
    if(!fgets(linha,sizeof(linha),f)){ fclose(f); return; }
    if(linha[0]=='#'){
        int nd; double nm, ct;
        if(sscanf(linha+1,"%d;%lf;%lf",&nd,&nm,&ct)==3){
            if(nd>=1 && nd<=MAX_DISC) g_cfg.ndisc=nd;
            if(nm>0) g_cfg.nota_max=nm;
            g_cfg.corte=ct;
        }
    }
    // header 2 nomes
    if(!fgets(linha,sizeof(linha),f)){ fclose(f); return; }
    strip_newline(linha);
    {
        int i=0; char *tok=strtok(linha,";");
        while(tok && i<g_cfg.ndisc){ strncpy(g_cfg.nomes_disc[i],tok,31); g_cfg.nomes_disc[i][31]='\0'; i++; tok=strtok(NULL,";"); }
        for(;i<g_cfg.ndisc;i++) snprintf(g_cfg.nomes_disc[i],32,"Disc_%d",i+1);
    }
    // header 3 pesos
    if(!fgets(linha,sizeof(linha),f)){ fclose(f); return; }
    strip_newline(linha);
    {
        int i=0; char *tok=strtok(linha,";");
        while(tok && i<g_cfg.ndisc){ g_cfg.pesos[i]=atof(tok); if(g_cfg.pesos[i]<=0) g_cfg.pesos[i]=1.0; i++; tok=strtok(NULL,";"); }
        for(;i<g_cfg.ndisc;i++) g_cfg.pesos[i]=1.0;
    }
    // header 4 admin
    if(fgets(linha,sizeof(linha),f)){
        if(strncmp(linha,"#admin=",7)==0){
            strip_newline(linha);
            strncpy(g_cfg.admin_pass, linha+7, sizeof(g_cfg.admin_pass)-1);
            g_cfg.admin_pass[sizeof(g_cfg.admin_pass)-1]='\0';
        } else {
            // não era admin, volta um passo no arquivo
            fseek(f, -(long)strlen(linha), SEEK_CUR);
        }
    }
    // dados
    g_n=0;
    while(fgets(linha,sizeof(linha),f) && g_n<MAX_ALUNOS){
        strip_newline(linha);
        if(!linha[0]) continue;
        char *tok;
        tok=strtok(linha,";"); if(!tok) continue; strncpy(g_alunos[g_n].matricula,tok,23); g_alunos[g_n].matricula[23]='\0';
        tok=strtok(NULL,";"); if(!tok) continue; strncpy(g_alunos[g_n].nome,tok,MAX_NOME-1); g_alunos[g_n].nome[MAX_NOME-1]='\0';
        tok=strtok(NULL,";"); if(!tok) continue; strncpy(g_alunos[g_n].turma,tok,MAX_TURMA-1); g_alunos[g_n].turma[MAX_TURMA-1]='\0';
        tok=strtok(NULL,";"); if(!tok) continue; g_alunos[g_n].rec=atof(tok);
        tok=strtok(NULL,";"); if(!tok) continue; g_alunos[g_n].presencas=atoi(tok);
        tok=strtok(NULL,";"); if(!tok) continue; g_alunos[g_n].aulas=atoi(tok);
        for(int j=0;j<g_cfg.ndisc;j++){ tok=strtok(NULL,";"); g_alunos[g_n].notas[j]= tok? atof(tok):0.0; }
        g_alunos[g_n].ndisc=g_cfg.ndisc;
        recalcula_um(&g_alunos[g_n]);
        g_n++;
    }
    fclose(f);
    g_dirty=0;
    audit("Carregar banco");
}

// ========================= Backup/Undo =====================
static void snapshot_make(){
    memcpy(g_prev_alunos, g_alunos, sizeof(Aluno)*g_n);
    g_prev_n = g_n;
    g_prev_valid = 1;
}
static void snapshot_undo(){
    if(!g_prev_valid){ puts("Nada a desfazer."); return; }
    memcpy(g_alunos, g_prev_alunos, sizeof(Aluno)*g_prev_n);
    g_n = g_prev_n;
    g_prev_valid = 0;
    g_dirty = 1;
    recalcula_todos();
    puts("Desfeito.");
    audit("Desfazer ultima alteracao");
}

// ========================= Busca ==========================
static int idx_por_matricula(const char *m){
    for(int i=0;i<g_n;i++) if(strcmp(g_alunos[i].matricula,m)==0) return i;
    return -1;
}

// ========================= Operações ======================
static void configurar(){
    char b[256];
    printf("Disciplinas atuais: %d (1..%d): ", g_cfg.ndisc, MAX_DISC);
    int nd = ler_int(); if(nd>=1 && nd<=MAX_DISC) g_cfg.ndisc=nd;

    printf("Nota maxima atual: %.2f (>0): ", g_cfg.nota_max);
    double nm = ler_double(); if(nm>0) g_cfg.nota_max=nm;

    printf("Media de corte atual: %.2f (0..%.2f): ", g_cfg.corte, g_cfg.nota_max);
    double ct = ler_double(); if(ct>=0 && ct<=g_cfg.nota_max) g_cfg.corte=ct;

    printf("Nomear disciplinas? (s/n): "); ler_linha(b,sizeof(b));
    if(b[0]=='s'||b[0]=='S'){
        for(int i=0;i<g_cfg.ndisc;i++){
            printf("  Disciplina %d: ", i+1);
            ler_linha(g_cfg.nomes_disc[i], sizeof(g_cfg.nomes_disc[i]));
            if(!g_cfg.nomes_disc[i][0]) snprintf(g_cfg.nomes_disc[i],32,"Disc_%d",i+1);
        }
    } else {
        for(int i=0;i<g_cfg.ndisc;i++) if(!g_cfg.nomes_disc[i][0]) snprintf(g_cfg.nomes_disc[i],32,"Disc_%d",i+1);
    }
    puts("Definir pesos relativos? (ex: 1 = todos iguais)");
    for(int i=0;i<g_cfg.ndisc;i++){
        printf("  Peso de %s: ", g_cfg.nomes_disc[i]);
        double w = ler_double(); if(w<=0) w=1.0; g_cfg.pesos[i]=w;
    }
    salvar_cfg();
    recalcula_todos();
    g_dirty=1;
    audit("Configurar turma/pesos");
}

static void cadastrar(){
    if(g_n>=MAX_ALUNOS){ puts("Limite atingido."); return; }
    snapshot_make();
    Aluno a; memset(&a,0,sizeof(a));
    printf("Nome do aluno: "); ler_linha(a.nome,sizeof(a.nome));
    if(!a.nome[0]) strcpy(a.nome,"Aluno");
    printf("Matricula (unica): "); ler_linha(a.matricula,sizeof(a.matricula));
    if(!a.matricula[0]){ snprintf(a.matricula,sizeof(a.matricula),"M%ld",(long)(time(NULL)%1000000)); }
    if(idx_por_matricula(a.matricula)!=-1){ puts("Matricula existente."); return; }
    printf("Turma (ex: 8M2): "); ler_linha(a.turma,sizeof(a.turma)); if(!a.turma[0]) strcpy(a.turma,"8M2");
    a.ndisc=g_cfg.ndisc;
    for(int i=0;i<g_cfg.ndisc;i++) a.notas[i]=0.0;
    a.rec=-1; a.aulas=0; a.presencas=0;
    recalcula_um(&a);
    g_alunos[g_n++]=a;
    g_dirty=1;
    audit("Cadastrar aluno");
    puts("Aluno cadastrado.");
}

static void listar(){
    if(g_n==0){ puts("Sem alunos."); return; }
    printf("\n--- ALUNOS (%d) ---\n", g_n);
    for(int i=0;i<g_n;i++){
        double freq = (g_alunos[i].aulas>0)? 100.0*g_alunos[i].presencas/g_alunos[i].aulas : 0.0;
        printf("%3d) %-20s Mat:%-10s Turma:%-8s Med:%.2f Fin:%.2f %s Freq:%5.1f%%\n",
               i+1, g_alunos[i].nome, g_alunos[i].matricula, g_alunos[i].turma,
               g_alunos[i].media, g_alunos[i].media_final, g_alunos[i].aprovado?"(A)":"(R)", freq);
    }
}

static void editar_notas(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char m[32];
    printf("Matricula: "); ler_linha(m,sizeof(m));
    int i=idx_por_matricula(m);
    if(i==-1){ puts("Nao encontrado."); return; }
    snapshot_make();
    for(int d=0; d<g_cfg.ndisc; d++){
        printf("  %s (0..%.2f) [atual %.2f]: ", g_cfg.nomes_disc[d], g_cfg.nota_max, g_alunos[i].notas[d]);
        double x=ler_double(); if(x<0)x=0; if(x>g_cfg.nota_max)x=g_cfg.nota_max;
        g_alunos[i].notas[d]=x;
    }
    printf("Nota de RECUPERACAO (-1 se nao tem) [atual %.2f]: ", g_alunos[i].rec);
    double r=ler_double(); if(r<-1) r=-1; if(r>g_cfg.nota_max) r=g_cfg.nota_max; g_alunos[i].rec=r;
    recalcula_um(&g_alunos[i]);
    g_dirty=1;
    audit("Editar notas/recuperacao");
    puts("Atualizado.");
}

static void marcar_aula(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char turma[MAX_TURMA];
    printf("Turma para registrar aula: "); ler_linha(turma,sizeof(turma));
    // conta quantos
    int count=0; for(int i=0;i<g_n;i++) if(strcmp(g_alunos[i].turma,turma)==0) count++;
    if(count==0){ puts("Turma vazia."); return; }
    snapshot_make();
    printf("Registrar presenca aluno a aluno (s/n)? "); char b[8]; ler_linha(b,sizeof(b));
    if(b[0]=='s'||b[0]=='S'){
        for(int i=0;i<g_n;i++){
            if(strcmp(g_alunos[i].turma,turma)!=0) continue;
            printf("Presenca de %-20s (s/n): ", g_alunos[i].nome);
            char p[8]; ler_linha(p,sizeof(p));
            g_alunos[i].aulas++;
            if(p[0]=='s'||p[0]=='S') g_alunos[i].presencas++;
        }
    } else {
        printf("Marcar todos como presentes (s) ou ausentes (n)? "); char p[8]; ler_linha(p,sizeof(p));
        for(int i=0;i<g_n;i++){
            if(strcmp(g_alunos[i].turma,turma)!=0) continue;
            g_alunos[i].aulas++;
            if(p[0]=='s'||p[0]=='S') g_alunos[i].presencas++;
        }
    }
    g_dirty=1;
    audit("Marcar aula/presenca");
    puts("Aula registrada.");
}

static int cmp_media_desc(const void *A,const void *B){
    const Aluno *a=(const Aluno*)A, *b=(const Aluno*)B;
    if(b->media_final > a->media_final) return 1;
    if(b->media_final < a->media_final) return -1;
    return strcmp(a->nome,b->nome);
}

static void dashboard(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char turma[MAX_TURMA];
    printf("Turma para dashboard (ENTER = todas): ");
    ler_linha(turma,sizeof(turma));
    // coleta
    int n=0; Aluno *buf = (Aluno*)malloc(sizeof(Aluno)*g_n);
    for(int i=0;i<g_n;i++){
        if(turma[0]==0 || strcmp(g_alunos[i].turma,turma)==0) buf[n++]=g_alunos[i];
    }
    if(n==0){ puts("Nada a mostrar."); free(buf); return; }

    // estatísticas
    double mg=0, s2=0, mx=-1e9, mn=1e9; int aprov=0;
    for(int i=0;i<n;i++){
        double m=buf[i].media_final; mg+=m; s2+=m*m; if(m>mx)mx=m; if(m<mn)mn=m;
        if(buf[i].aprovado) aprov++;
    }
    mg/=n; double dp = sqrt(fmax(0.0, s2/n - mg*mg));
    double perc = 100.0*aprov/n;

    // ranking
    qsort(buf,n,sizeof(Aluno),cmp_media_desc);

    printf("\n===== DASHBOARD %s =====\n", turma[0]? turma : "(todas)");
    printf("Disc: %d | NotaMax: %.2f | Corte: %.2f\n", g_cfg.ndisc, g_cfg.nota_max, g_cfg.corte);
    printf("Media: %.2f | Maior: %.2f | Menor: %.2f | Desvio: %.2f | Aprov: %d/%d (%.1f%%)\n",
           mg,mx,mn,dp,aprov,n,perc);

    printf("\nRanking:\n");
    for(int i=0;i<n;i++){
        double freq = (buf[i].aulas>0)? 100.0*buf[i].presencas/buf[i].aulas : 0.0;
        printf("%2d) %-20s Tur:%-8s MedFin:%.2f %s Freq:%5.1f%%\n",
               i+1, buf[i].nome, buf[i].turma, buf[i].media_final, buf[i].aprovado?"(A)":"(R)", freq);
    }

    printf("\nGrafico (media final por aluno):\n");
    for(int i=0;i<n;i++){
        printf("%-15s | ", buf[i].nome);
        barra(buf[i].media_final, g_cfg.nota_max, 32, stdout);
        printf(" | %.2f\n", buf[i].media_final);
    }

    int bins[5]={0};
    for(int i=0;i<n;i++){
        double p = g_cfg.nota_max>0 ? buf[i].media_final/g_cfg.nota_max : 0.0;
        if(p<0.2) bins[0]++; else if(p<0.4) bins[1]++; else if(p<0.6) bins[2]++; else if(p<0.8) bins[3]++; else bins[4]++;
    }
    const char *lab[5]={"0-20%","20-40%","40-60%","60-80%","80-100%"};
    printf("\nHistograma:\n");
    for(int i=0;i<5;i++){
        printf("%-8s | ", lab[i]);
        for(int k=0;k<bins[i];k++) putchar('*');
        printf(" (%d)\n", bins[i]);
    }
    printf("=====================================\n");
    free(buf);
}

static void exportar_txt_turma(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char turma[MAX_TURMA];
    printf("Turma para exportar (ENTER = todas): ");
    ler_linha(turma,sizeof(turma));
    char tstamp[32]; ts_file(tstamp,sizeof(tstamp));
    char path[256];
    snprintf(path,sizeof(path), DATA_DIR"/relatorio_%s_%s.txt", turma[0]?turma:"todas", tstamp);
    FILE *f=fopen(path,"w"); if(!f){ puts("Falha ao criar TXT."); return; }

    // coleta
    int n=0; Aluno *buf = (Aluno*)malloc(sizeof(Aluno)*g_n);
    for(int i=0;i<g_n;i++) if(!turma[0] || strcmp(g_alunos[i].turma,turma)==0) buf[n++]=g_alunos[i];
    // estatísticas
    double mg=0,s2=0,mx=-1e9,mn=1e9; int aprov=0;
    for(int i=0;i<n;i++){ double m=buf[i].media_final; mg+=m; s2+=m*m; if(m>mx)mx=m; if(m<mn)mn=m; if(buf[i].aprovado)aprov++; }
    mg/= (n? n:1); double dp = sqrt(fmax(0.0, s2/(n? n:1) - mg*mg));
    double perc = n? 100.0*aprov/n : 0.0;

    fprintf(f,"RELATORIO %s (%s)\n", turma[0]?turma:"TODAS", tstamp);
    fprintf(f,"Disc:%d | NotaMax:%.2f | Corte:%.2f\n", g_cfg.ndisc, g_cfg.nota_max, g_cfg.corte);
    fprintf(f,"Media:%.2f | Maior:%.2f | Menor:%.2f | Desvio:%.2f | Aprov:%d/%d (%.1f%%)\n\n",
            mg,mx,mn,dp,aprov,n,perc);

     for(int i=0;i<n;i++){
        double freq = (buf[i].aulas>0)? 100.0*buf[i].presencas/buf[i].aulas : 0.0;
        fprintf(f,"%-20s Mat:%-10s Tur:%-8s Media:%.2f Final:%.2f %s Freq:%5.1f%%\n",
                buf[i].nome, buf[i].matricula, buf[i].turma, buf[i].media, buf[i].media_final,
                buf[i].aprovado?"(A)":"(R)", freq);
    }

    fprintf(f,"\nGrafico (media final):\n");
    for(int i=0;i<n;i++){
        fprintf(f,"%-15s | ", buf[i].nome);
        barra(buf[i].media_final, g_cfg.nota_max, 32, f);
        fprintf(f," | %.2f\n", buf[i].media_final);
    }
    fclose(f); free(buf);
    printf("TXT salvo: %s\n", path);
    audit("Exportar TXT turma");
}

static void exportar_csv_turma(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char turma[MAX_TURMA];
    printf("Turma para exportar (ENTER = todas): ");
    ler_linha(turma,sizeof(turma));
    char tstamp[32]; ts_file(tstamp,sizeof(tstamp));
    char path[256];
    snprintf(path,sizeof(path), DATA_DIR"/relatorio_%s_%s.csv", turma[0]?turma:"todas", tstamp);
    FILE *f=fopen(path,"w"); if(!f){ puts("Falha ao criar CSV."); return; }

    fprintf(f,"Turma,%s\n", turma[0]?turma:"TODAS");
    fprintf(f,"Disciplinas,%d\nNotaMax,%.2f\nCorte,%.2f\n\n", g_cfg.ndisc, g_cfg.nota_max, g_cfg.corte);
    fprintf(f,"Matricula,Nome,Turma,Recuperacao,Presencas,Aulas");
    for(int i=0;i<g_cfg.ndisc;i++) fprintf(f,",%s", g_cfg.nomes_disc[i]);
    fprintf(f,",Media,Final,Situacao,Freq(%%)\n");

    for(int i=0;i<g_n;i++){
        if(turma[0] && strcmp(g_alunos[i].turma,turma)!=0) continue;
        char nome_s[MAX_NOME]; strncpy(nome_s,g_alunos[i].nome,MAX_NOME-1); nome_s[MAX_NOME-1]='\0'; for(char*p=nome_s;*p;++p) if(*p==',') *p=';';
        double freq = (g_alunos[i].aulas>0)? 100.0*g_alunos[i].presencas/g_alunos[i].aulas : 0.0;
        fprintf(f,"%s,%s,%s,%.2f,%d,%d", g_alunos[i].matricula, nome_s, g_alunos[i].turma,
                g_alunos[i].rec, g_alunos[i].presencas, g_alunos[i].aulas);
        for(int j=0;j<g_cfg.ndisc;j++) fprintf(f,",%.2f", g_alunos[i].notas[j]);
        fprintf(f,",%.2f,%.2f,%s,%.1f\n", g_alunos[i].media, g_alunos[i].media_final,
                g_alunos[i].aprovado?"APROVADO":"REPROVADO", freq);
    }
    fclose(f);
    printf("CSV salvo: %s\n", path);
    audit("Exportar CSV turma");
}

static void exportar_txt_individual(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char m[32]; printf("Matricula: "); ler_linha(m,sizeof(m));
    int i=idx_por_matricula(m); if(i==-1){ puts("Nao encontrado."); return; }
    char tstamp[32]; ts_file(tstamp,sizeof(tstamp));
    char path[256], nome_s[MAX_NOME]; strncpy(nome_s,g_alunos[i].nome,MAX_NOME-1); nome_s[MAX_NOME-1]='\0';
    for(char*p=nome_s;*p;++p) if(*p==' ') *p='_';
    snprintf(path,sizeof(path), DATA_DIR"/aluno_%s_%s.txt", nome_s, tstamp);
    FILE *f=fopen(path,"w"); if(!f){ puts("Falha ao criar TXT individual."); return; }

    fprintf(f,"Relatorio Individual - %s (%s)\n", g_alunos[i].nome, g_alunos[i].turma);
    fprintf(f,"Matricula: %s\n\n", g_alunos[i].matricula);
    for(int d=0; d<g_cfg.ndisc; d++)
        fprintf(f,"%s: %.2f\n", g_cfg.nomes_disc[d], g_alunos[i].notas[d]);
    fprintf(f,"\nMedia: %.2f\nRecuperacao: %.2f\nFinal: %.2f -> %s\n",
            g_alunos[i].media, g_alunos[i].rec, g_alunos[i].media_final,
            g_alunos[i].aprovado?"APROVADO":"REPROVADO");
    fprintf(f,"Grafico: "); barra(g_alunos[i].media_final, g_cfg.nota_max, 30, f); fprintf(f," | %.2f\n", g_alunos[i].media_final);
    fclose(f);
    printf("TXT salvo: %s\n", path);
    audit("Exportar TXT individual");
}

static void buscar(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char m[32]; printf("Matricula: "); ler_linha(m,sizeof(m));
    int i=idx_por_matricula(m); if(i==-1){ puts("Nao encontrado."); return; }
    double freq = (g_alunos[i].aulas>0)? 100.0*g_alunos[i].presencas/g_alunos[i].aulas : 0.0;
    printf("%s | Mat:%s | Turma:%s | Media:%.2f | Final:%.2f | %s | Freq:%.1f%%\n",
           g_alunos[i].nome, g_alunos[i].matricula, g_alunos[i].turma,
           g_alunos[i].media, g_alunos[i].media_final,
           g_alunos[i].aprovado?"APROVADO":"REPROVADO", freq);
}

static void remover(){
    if(g_n==0){ puts("Sem alunos."); return; }
    char m[32]; printf("Matricula para remover: "); ler_linha(m,sizeof(m));
    int i=idx_por_matricula(m); if(i==-1){ puts("Nao encontrado."); return; }
    snapshot_make();
    for(int k=i;k<g_n-1;k++) g_alunos[k]=g_alunos[k+1];
    g_n--; g_dirty=1;
    audit("Remover aluno");
    puts("Removido.");
}

static void importar_lista(){
    char path[256];
    printf("CSV de lista (nome;matricula;turma por linha): ");
    ler_linha(path,sizeof(path));
    FILE *f=fopen(path,"r"); if(!f){ puts("Nao abriu arquivo."); return; }
    snapshot_make();
    char lin[512];
    int add=0;
    while(fgets(lin,sizeof(lin),f)){
        strip_newline(lin);
        if(!lin[0]) continue;
        char *nm=strtok(lin,";"); if(!nm) continue;
        char *mat=strtok(NULL,";"); if(!mat) continue;
        char *tur=strtok(NULL,";"); if(!tur) continue;
        if(idx_por_matricula(mat)!=-1) continue;
        if(g_n>=MAX_ALUNOS) break;
        Aluno a; memset(&a,0,sizeof(a));
        strncpy(a.nome,nm,MAX_NOME-1); strncpy(a.matricula,mat,23); strncpy(a.turma,tur,MAX_TURMA-1);
        a.ndisc=g_cfg.ndisc; for(int d=0;d<g_cfg.ndisc;d++) a.notas[d]=0.0; a.rec=-1; a.aulas=0; a.presencas=0;
        recalcula_um(&a);
        g_alunos[g_n++]=a; add++;
    }
    fclose(f);
    g_dirty=1;
    printf("Importados: %d\n", add);
    audit("Importar lista CSV");
}

static void mudar_senha(){
    char cur[64], nova[64];
    printf("Senha admin atual: "); ler_linha(cur,sizeof(cur));
    if(strcmp(cur,g_cfg.admin_pass)!=0){ puts("Senha incorreta."); return; }
    printf("Nova senha: "); ler_linha(nova,sizeof(nova));
    if(!nova[0]){ puts("Senha vazia ignorada."); return; }
    strncpy(g_cfg.admin_pass,nova,sizeof(g_cfg.admin_pass)-1); g_cfg.admin_pass[sizeof(g_cfg.admin_pass)-1]='\0';
    salvar_cfg();
    puts("Senha alterada.");
    audit("Alterar senha admin");
}

static void criar_backup(){
    char t[32]; ts_file(t,sizeof(t));
    char path[256]; snprintf(path,sizeof(path), DATA_DIR"/backup_%s.csv", t);
    salvar_db();
    // apenas copia o DB atual para backup nominal
    FILE *src=fopen(DB_PATH,"r"); if(!src){ puts("Banco inexistente para backup."); return; }
    FILE *dst=fopen(path,"w"); if(!dst){ fclose(src); puts("Falha backup."); return; }
    char buf[2048]; size_t n;
    while((n=fread(buf,1,sizeof(buf),src))>0) fwrite(buf,1,n,dst);
    fclose(src); fclose(dst);
    printf("Backup salvo: %s\n", path);
    audit("Criar backup nomeado");
}

static void restaurar_de_arquivo(){
    char path[256];
    printf("Arquivo CSV para restaurar (backup): ");
    ler_linha(path,sizeof(path));
    FILE *src=fopen(path,"r"); if(!src){ puts("Nao abriu backup."); return; }
    FILE *dst=fopen(DB_PATH,"w"); if(!dst){ fclose(src); puts("Falha gravacao."); return; }
    char buf[2048]; size_t n;
    while((n=fread(buf,1,sizeof(buf),src))>0) fwrite(buf,1,n,dst);
    fclose(src); fclose(dst);
    carregar_db();
    puts("Restaurado.");
    audit("Restaurar de arquivo");
}

// ========================= Menu ============================
static int autenticar_admin(){
    char s[64]; printf("Senha admin: "); ler_linha(s,sizeof(s));
    if(strcmp(s,g_cfg.admin_pass)==0) return 1;
    puts("Negado.");
    return 0;
}
static void menu(){
    while(1){
        printf("\n========== SISTEMA ESCOLAR ==========\n");
        printf("1) Configurar (disciplinas/pesos/corte)\n");
        printf("2) Cadastrar aluno\n");
        printf("3) Listar alunos\n");
        printf("4) Editar notas + recuperacao\n");
        printf("5) Registrar aula/presencas (por turma)\n");
        printf("6) Dashboard (estatisticas/ranking)\n");
        printf("7) Exportar TXT (turma ou todas)\n");
        printf("8) Exportar CSV (turma ou todas)\n");
        printf("9) Exportar TXT individual\n");
        printf("10) Buscar por matricula\n");
        printf("11) Remover por matricula\n");
        printf("12) Importar lista inicial (CSV nome;matricula;turma)\n");
        printf("13) Salvar banco\n");
        printf("14) Carregar banco\n");
        printf("15) Desfazer ultima alteracao\n");
        printf("16) Criar backup nomeado\n");
        printf("17) Restaurar de arquivo\n");
        printf("18) Trocar senha admin\n");
        printf("0) Sair\n");
        printf("Escolha: ");
        int op = ler_int();
        switch(op){
            case 0:
                if(g_dirty){ printf("Salvar alteracoes pendentes? (s/n): "); char b[8]; ler_linha(b,sizeof(b)); if(b[0]=='s'||b[0]=='S') salvar_db(); }
                puts("Encerrando."); return;
            case 1: if(autenticar_admin()) configurar(); break;
            case 2: cadastrar(); break;
            case 3: listar(); break;
            case 4: if(autenticar_admin()) editar_notas(); break;
            case 5: if(autenticar_admin()) marcar_aula(); break;
            case 6: dashboard(); break;
            case 7: exportar_txt_turma(); break;
            case 8: exportar_csv_turma(); break;
            case 9: exportar_txt_individual(); break;
            case 10: buscar(); break;
            case 11: if(autenticar_admin()) remover(); break;
            case 12: if(autenticar_admin()) importar_lista(); break;
            case 13: salvar_db(); break;
            case 14: carregar_db(); puts("Carregado."); break;
            case 15: snapshot_undo(); break;
            case 16: if(autenticar_admin()) criar_backup(); break;
            case 17: if(autenticar_admin()) restaurar_de_arquivo(); break;
            case 18: if(autenticar_admin()) mudar_senha(); break;
            default: puts("Opcao invalida.");
        }
    }
}

// ========================= Main ===========================
int main(void){
    ensure_dir();
    carregar_cfg();
    // nomes/pesos default se preciso
    for(int i=0;i<g_cfg.ndisc;i++){
        if(!g_cfg.nomes_disc[i][0]) snprintf(g_cfg.nomes_disc[i],32,"Disc_%d",i+1);
        if(g_cfg.pesos[i]<=0) g_cfg.pesos[i]=1.0;
    }
    carregar_db(); // ignora se nao existir
    audit("Iniciar aplicacao");
    menu();
    audit("Sair aplicacao");
    return 0;
