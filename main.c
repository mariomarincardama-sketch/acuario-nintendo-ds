/* ReefKeeper DS - gestor de acuario marino para Nintendo DS (libnds)
 * Pantalla de arriba: tu acuario en vivo. Pantalla de abajo: menus (botones).
 * Solo ASCII: la fuente de la consola de la DS no tiene acentos. */
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef USE_FAT
#include <fat.h>
#endif
#include "species.h"

#define MAXT 40
#define NPAR 10
#define NTASK 8
#define MAXL 420
#define PAGE 19
#define MAGIC 0x52454546u

typedef struct {
    unsigned magic;
    int ntank;
    int tank[MAXT];
    int dim[8];
    int pv[NPAR];
    int pset[NPAR];
    int last[NTASK];
} Save;

typedef struct { const char *n; const char *u; int mn, mx, dmn, dmx, step, def; } Par;
typedef struct { int k; const char *m; const char *c; } Cmp;

enum { M_MENU, M_CAT, M_DET, M_TANK, M_VOL, M_PAR, M_TASK, M_HELP };

static const char *DIMN[8] = { "Largo (cm)", "Ancho (cm)", "Alto (cm)", "Sin agua borde (cm)",
                               "Sustrato (L)", "Rocas/deco (L)", "Equipos (L)", "Sump (L)" };
/* valores x100 (p. ej. 2540 = 25.40) */
static const Par P[NPAR] = {
    { "Temperatura", "C", 2400, 2600, 2000, 3000, 10, 2500 },
    { "Salinidad", "ppt", 3300, 3600, 3000, 3800, 10, 3500 },
    { "pH", "", 800, 840, 780, 860, 5, 820 },
    { "KH", "dKH", 700, 1100, 600, 1300, 10, 900 },
    { "Calcio", "mg/L", 40000, 45000, 35000, 50000, 500, 42000 },
    { "Magnesio", "mg/L", 125000, 135000, 110000, 150000, 2500, 130000 },
    { "Amoniaco", "mg/L", 0, 5, 0, 10, 1, 0 },
    { "Nitrito", "mg/L", 0, 5, 0, 10, 1, 0 },
    { "Nitrato", "mg/L", 0, 1000, 0, 2500, 50, 500 },
    { "Fosfato", "mg/L", 0, 10, 0, 30, 1, 5 } };
static const char *TN[NTASK] = { "Cambio de agua", "Comprobar salinidad", "Limpiar cristales", "Limpiar skimmer",
                                 "Revisar bombas", "Cambiar mat. filtrante", "Mantenim. osmosis", "Calibrar sondas" };
static const int TE[NTASK] = { 14, 7, 7, 7, 30, 30, 90, 30 };
static const int FEEDH[2] = { 9, 19 };
static const char *TYP[3] = { "Pez", "Invertebrado", "Anemona" };
static const char *AGL[6] = { "Muy pacifico", "Pacifico", "Mod. territorial", "Territorial", "Agresivo", "Muy agresivo" };
static const char *DIFL[5] = { "?", "Facil", "Moderada", "Dificil", "Solo expertos" };
static const char *CATL[4] = { "Todas", "Peces", "Invert.", "Anemonas" };
static const char *MENUI[6] = { "Catalogo de especies", "Mi acuario", "Calculadora de litros", "Parametros del agua",
                                "Tareas y recordatorios", "Ayuda" };

static PrintConsole topc, botc;
static Save S;
static int mode = M_MENU, mcur = 0, ccur = 0, coff = 0, cat = 0, tcur = 0, vfield = 0, pcur = 0, pedit = 0, kcur = 0;
static int doff = 0, dsp = 0, dret = M_CAT, dirty = 1, frame = 0, haveFat = 0, msgt = 0;
static int fidx[NSP], nf = 0;
static char msg[32] = "";
static char LN[MAXL][32];
static int nL = 0;

/* ---------- utilidades ---------- */
static int today(void) { return (int)(time(NULL) / 86400); }
static void gotoxy(int r, int c) { iprintf("\x1b[%d;%dH", r, c); }
static void fm2(char *o, size_t n, int v) {
    int a = v < 0 ? -v : v;
    snprintf(o, n, "%s%d.%02d", v < 0 ? "-" : "", a / 100, a % 100);
}
static void vols(int *g, int *gap, int *net, int *tot) {
    long gr = (long)S.dim[0] * S.dim[1] * S.dim[2] / 1000;
    long gp = (long)S.dim[0] * S.dim[1] * S.dim[3] / 1000;
    long nt = gr - gp - S.dim[4] - S.dim[5] - S.dim[6];
    if (nt < 0) nt = 0;
    *g = (int)gr; *gap = (int)gp; *net = (int)nt; *tot = (int)nt + S.dim[7];
}
static int pstatus(int i) { /* -1 sin dato, 0 ok, 1 fuera, 2 peligro */
    int v;
    if (!S.pset[i]) return -1;
    v = S.pv[i];
    if (v < P[i].dmn || v > P[i].dmx) return 2;
    if (v < P[i].mn || v > P[i].mx) return 1;
    return 0;
}
static int nalerts(void) {
    int i, n = 0;
    for (i = 0; i < NPAR; i++) if (pstatus(i) > 0) n++;
    return n;
}
static int noverdue(void) {
    int i, n = 0, t = today();
    for (i = 0; i < NTASK; i++) if (TE[i] - (t - S.last[i]) < 0) n++;
    return n;
}

/* ---------- guardado (tarjeta SD / flashcart, si hay) ---------- */
static void save(void) {
#ifdef USE_FAT
    FILE *f;
    if (!haveFat) return;
    f = fopen("/reefkeeper.sav", "wb");
    if (!f) return;
    fwrite(&S, sizeof(S), 1, f);
    fclose(f);
#endif
}
static void load(void) {
    int i, td = today();
    memset(&S, 0, sizeof(S));
    S.dim[0] = 100; S.dim[1] = 70; S.dim[2] = 100; S.dim[3] = 5;
#ifdef USE_FAT
    if (haveFat) {
        FILE *f = fopen("/reefkeeper.sav", "rb");
        if (f) {
            Save t;
            if (fread(&t, sizeof(t), 1, f) == 1 && t.magic == MAGIC) S = t;
            fclose(f);
        }
    }
#else
    (void)haveFat;
#endif
    S.magic = MAGIC;
    if (S.ntank < 0 || S.ntank > MAXT) S.ntank = 0;
    for (i = 0; i < S.ntank; i++) if (S.tank[i] < 0 || S.tank[i] >= NSP) S.tank[i] = 0;
    for (i = 0; i < NTASK; i++) if (S.last[i] == 0) S.last[i] = td;
}

/* ---------- compatibilidad (misma logica que la app del movil) ---------- */
static Cmp comp(const Sp *a, const Sp *b) {
    Cmp r;
    int mx, mn;
    r.k = 0; r.m = "Sin conflicto importante segun los datos."; r.c = "";
    if (a->tipo == 2 || b->tipo == 2) {
        const Sp *an = (a->tipo == 2) ? a : b;
        const Sp *o = (an == a) ? b : a;
        if (o->tipo == 2) {
            if (o == an) { r.k = 1; r.m = "Misma especie: pueden fusionarse o competir."; r.c = "Ejemplares pequenos y mucha distancia."; }
            else { r.k = 2; r.m = "Anemonas distintas se pican y compiten."; r.c = "Evita juntarlas salvo en acuarios enormes."; }
            return r;
        }
        if (strcmp(o->grp, "Amphiprion") == 0) {
            r.k = 1;
            if (an->host) { r.m = "Pueden hacer simbiosis, pero no siempre ocurre."; r.c = "Anemona primero, en acuario maduro. No forzar."; }
            else { r.m = "No es hospedadora natural de este payaso."; r.c = "Observa de cerca."; }
            return r;
        }
        if (o->tipo == 1) { r.k = 1; r.m = "Puede capturar invertebrados pequenos."; r.c = "Vigila y deja espacio."; return r; }
        if (an->pred) { r.k = 1; r.m = "Puede capturar peces pequenos."; r.c = "Evita peces pequenos cerca."; return r; }
        return r;
    }
    if (a->tipo == 0 && b->tipo == 0) {
        mx = a->ag > b->ag ? a->ag : b->ag;
        mn = a->ag < b->ag ? a->ag : b->ag;
        if (a == b) {
            if (a->ag >= 3) { r.k = 2; r.m = "Dos ejemplares territoriales suelen pelear."; r.c = "Solo en acuarios muy grandes; mejor uno solo."; }
            else { r.k = 1; r.m = "Misma especie: pareja o pelea segun sexo y edad."; r.c = "Juveniles juntos o pareja formada, con refugios."; }
        } else if (strcmp(a->grp, b->grp) == 0 && mx >= 2) {
            r.k = 2; r.m = "Mismo genero y territoriales: compiten por el espacio."; r.c = "Evita combinarlos salvo en acuarios muy grandes.";
        } else if (mx >= 4) {
            r.k = 2; r.m = "Una especie es agresiva y puede danar a la otra."; r.c = "Evitar la combinacion.";
        } else if (mx >= 3 && mn >= 3) {
            r.k = 1; r.m = "Ambas son territoriales y se disputan el espacio."; r.c = "Acuario amplio e introducir la mas territorial la ultima.";
        } else if (mx >= 3 && mn <= 1) {
            r.k = 1; r.m = "La territorial puede acosar a la pacifica."; r.c = "Muchos refugios; la territorial entra la ultima.";
        }
        return r;
    }
    { /* pez + invertebrado */
        const Sp *f = (a->tipo == 0) ? a : b;
        if (f->ag >= 4) { r.k = 2; r.m = "El pez agresivo puede atacar al invertebrado."; r.c = "Evitar la combinacion."; }
        else if (f->ag >= 3) { r.k = 1; r.m = "Los peces territoriales molestan a los invertebrados."; r.c = "Refugios y vigilancia."; }
    }
    return r;
}
static const char *CLAB[3] = { "OK", "CONDICIONADA", "ALTO RIESGO" };

/* ---------- texto con saltos de linea ---------- */
static void addl(const char *s) {
    if (nL < MAXL) { strncpy(LN[nL], s, 31); LN[nL][31] = 0; nL++; }
}
static void addw(const char *s) {
    const int w = 30;
    size_t n = strlen(s), i = 0, len, cut;
    char buf[32];
    while (i < n) {
        while (i < n && s[i] == ' ') i++;
        if (i >= n) break;
        len = n - i;
        if (len > (size_t)w) {
            cut = w;
            while (cut > 0 && s[i + cut] != ' ') cut--;
            if (cut == 0) cut = w;
            len = cut;
        }
        memcpy(buf, s + i, len); buf[len] = 0;
        addl(buf);
        i += len;
    }
}
static void addkv(const char *k, const char *v) {
    char b[400];
    snprintf(b, sizeof(b), "%s%s", k, v);
    addw(b);
}
static void buildDetail(int idx) {
    const Sp *s = &SPECIES[idx];
    char b[96];
    int i, g, gap, net, tot, seen[NSP];
    nL = 0; doff = 0;
    addw(s->n);
    addl(s->c);
    snprintf(b, sizeof(b), "%s - %s", TYP[s->tipo], AGL[s->ag]);
    addl(b);
    snprintf(b, sizeof(b), "Adulto: %d cm  Min: %d L", s->tam, s->vol);
    addl(b);
    snprintf(b, sizeof(b), "Dificultad: %s", DIFL[s->dif]);
    addl(b);
    addw(s->dtx);
    addl("");
    addl("-- ALIMENTACION --");
    addkv("Que: ", s->fq);
    addkv("Cuanto: ", s->ff);
    addkv("Ojo: ", s->fo);
    addl("");
    addl("-- NOTA --");
    addw(s->nota);
    addl("");
    addl("-- COMPATIBILIDAD --");
    vols(&g, &gap, &net, &tot);
    if (net > 0) {
        if (net < s->vol) { snprintf(b, sizeof(b), "VOLUMEN: tu acuario (%d L) es menor que el minimo (%d L). ALTO RIESGO", net, s->vol); addw(b); }
        else addl("Volumen: suficiente (minimo).");
    } else addl("Calcula tu volumen para comprobarlo.");
    memset(seen, 0, sizeof(seen));
    if (S.ntank == 0) addl("Tu acuario esta vacio.");
    for (i = 0; i < S.ntank; i++) {
        const Sp *o = &SPECIES[S.tank[i]];
        Cmp c;
        if (seen[S.tank[i]]) continue;
        seen[S.tank[i]] = 1;
        c = comp(s, o);
        snprintf(b, sizeof(b), "vs %s: %s", o->n, CLAB[c.k]);
        addw(b);
        if (c.k > 0) { addw(c.m); if (c.c[0]) { snprintf(b, sizeof(b), "Para reducir: %s", c.c); addw(b); } }
    }
    addl("");
    addw("Datos orientativos y de demostracion: contrastalos con fuentes especializadas.");
}
static void buildFilter(void) {
    int i;
    nf = 0;
    for (i = 0; i < NSP; i++)
        if (cat == 0 || SPECIES[i].tipo == cat - 1) fidx[nf++] = i;
    if (ccur >= nf) ccur = nf ? nf - 1 : 0;
    if (ccur < coff) coff = ccur;
}

/* ---------- pantalla de arriba: acuario en vivo ---------- */
static void putat(char scr[24][33], int r, int c, const char *s) {
    int i;
    if (r < 0 || r > 23) return;
    for (i = 0; s[i] && c + i < 31; i++) if (c + i >= 0) scr[r][c + i] = s[i];
}
static void drawTop(void) {
    static char scr[24][33];
    char b[64], x[16], y[16], z[16];
    int r, i, k, nfish = 0, ndw = 0, nowm, bestd = 99999, d, ap = 0;
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    int hh = tm ? tm->tm_hour : 0, mi = tm ? tm->tm_min : 0;
    for (r = 0; r < 24; r++) { memset(scr[r], ' ', 31); scr[r][31] = 0; scr[r][32] = 0; }
    putat(scr, 0, 1, "ReefKeeper DS  -  Tu acuario");
    for (k = 0; k < 5; k++) { r = 13 - ((frame / 5 + k * 3) % 12); putat(scr, r, 3 + k * 6 + (frame / 9 + k) % 2, (k & 1) ? "o" : "."); }
    for (i = 0; i < S.ntank; i++) {
        const Sp *s = &SPECIES[S.tank[i]];
        if (s->tipo == 0 && nfish < 7) {
            int span = 26, p = (frame / 4 + nfish * 7 + nfish * nfish) % (2 * span), xx = p < span ? p : 2 * span - p;
            putat(scr, 1 + (nfish * 2) % 11, xx, p < span ? "><>" : "<><");
            nfish++;
        } else if (s->tipo == 2 && ndw < 4) {
            putat(scr, 13, 2 + ndw * 7, ((frame / 12) & 1) ? "*Y*" : "Y*Y");
            ndw++;
        } else if (s->tipo == 1 && ap < 4) {
            int span = 24, p = (frame / 14 + ap * 9) % (2 * span), xx = p < span ? p : 2 * span - p;
            putat(scr, 12, xx, "o~");
            ap++;
        }
    }
    for (i = 0; i < 31; i++) scr[14][i] = (i & 1) ? ':' : '.';
    snprintf(b, sizeof(b), "Hora %02d:%02d   Tareas venc: %d", hh, mi, noverdue());
    putat(scr, 15, 0, b);
    if (S.pset[0]) fm2(x, sizeof(x), S.pv[0]); else snprintf(x, sizeof(x), "--");
    if (S.pset[1]) fm2(y, sizeof(y), S.pv[1]); else snprintf(y, sizeof(y), "--");
    if (S.pset[2]) fm2(z, sizeof(z), S.pv[2]); else snprintf(z, sizeof(z), "--");
    snprintf(b, sizeof(b), "T %.7s C Sal %.7s pH %.7s", x, y, z);
    putat(scr, 16, 0, b);
    nowm = hh * 60 + mi;
    for (i = 0; i < 2; i++) { d = (FEEDH[i] * 60 - nowm + 1440) % 1440; if (d < bestd) bestd = d; }
    if (bestd == 0) snprintf(b, sizeof(b), "Proxima comida: AHORA");
    else snprintf(b, sizeof(b), "Proxima comida en %02d:%02d", bestd / 60, bestd % 60);
    putat(scr, 17, 0, b);
    snprintf(b, sizeof(b), "Alertas: %d   Habitantes: %d", nalerts(), S.ntank);
    putat(scr, 18, 0, b);
    for (i = 0; i < NPAR; i++) {
        int st = pstatus(i);
        if (st > 0) {
            fm2(x, sizeof(x), S.pv[i]);
            snprintf(b, sizeof(b), "%s %s %s %s", st == 2 ? "PELIGRO:" : "Fuera:", P[i].n, x, P[i].u);
            putat(scr, 20, 0, b);
            putat(scr, 21, 0, "Verifica y corrige poco a poco.");
            break;
        }
    }
    if (i == NPAR) putat(scr, 20, 0, "Sin alertas con tus datos.");
    putat(scr, 23, 0, msgt > 0 ? msg : "Observa a tus animales a diario");
    consoleSelect(&topc);
    gotoxy(0, 0);
    for (r = 0; r < 24; r++) {
        scr[r][31] = 0;
        if (r < 23) iprintf("%s\n", scr[r]); else iprintf("%s", scr[r]);
    }
}

/* ---------- pantalla de abajo ---------- */
static void foot(const char *s) { gotoxy(23, 0); iprintf("%s", s); }
static void drawBottom(void) {
    int i, n;
    char x[16];
    consoleSelect(&botc);
    consoleClear();
    if (mode == M_MENU) {
        iprintf(" REEFKEEPER DS  -  MENU\n\n");
        for (i = 0; i < 6; i++) iprintf(" %c %s\n\n", i == mcur ? '>' : ' ', MENUI[i]);
        foot("UP/DOWN mover   A elegir");
    } else if (mode == M_CAT) {
        iprintf(" CATALOGO [%s] %d\n\n", CATL[cat], nf);
        for (i = coff; i < nf && i < coff + PAGE; i++)
            iprintf("%c%-29.29s\n", i == ccur ? '>' : ' ', SPECIES[fidx[i]].n);
        foot("A ver  L/R tipo  B menu");
    } else if (mode == M_DET) {
        for (i = doff; i < nL && i < doff + 22; i++) iprintf("%s\n", LN[i]);
        foot("UP/DN  X anadir  B atras");
    } else if (mode == M_TANK) {
        iprintf(" MI ACUARIO (%d/%d)\n\n", S.ntank, MAXT);
        if (S.ntank == 0) iprintf(" Vacio. Anade desde el catalogo\n con X.\n");
        for (i = 0; i < S.ntank && i < 19; i++) {
            int j = (tcur >= 19) ? tcur - 18 + i : i;
            if (j >= S.ntank) break;
            iprintf("%c%-29.29s\n", j == tcur ? '>' : ' ', SPECIES[S.tank[j]].n);
        }
        foot("A ficha  Y quitar  B menu");
    } else if (mode == M_VOL) {
        int g, gap, net, tot;
        vols(&g, &gap, &net, &tot);
        iprintf(" CALCULADORA DE LITROS\n\n");
        for (i = 0; i < 8; i++) iprintf("%c%-21.21s %5d\n", i == vfield ? '>' : ' ', DIMN[i], S.dim[i]);
        iprintf("\n Bruto:   %d L\n Neto:    %d L\n Total:   %d L\n", g, net, tot);
        iprintf(" (sin agua borde: %d L)\n", gap);
        foot("L/R campo UP/DN +-1(R:10)");
    } else if (mode == M_PAR) {
        iprintf(" PARAMETROS DEL AGUA\n\n");
        for (i = 0; i < NPAR; i++) {
            const char *st = "";
            int s = pstatus(i);
            if (S.pset[i]) fm2(x, sizeof(x), S.pv[i]); else snprintf(x, sizeof(x), "--");
            st = s == 2 ? "PELIGRO" : s == 1 ? "FUERA" : s == 0 ? "ok" : "";
            iprintf("%c%-11.11s %9.9s %-7.7s\n", i == pcur ? (pedit ? '*' : '>') : ' ', P[i].n, x, st);
        }
        n = pcur;
        fm2(x, sizeof(x), P[n].mn);
        { char y[16]; fm2(y, sizeof(y), P[n].mx); iprintf("\n Objetivo: %s a %s %s\n", x, y, P[n].u); }
        iprintf(" Rangos orientativos.\n");
        foot(pedit ? "UP/DN valor  A guardar" : "A editar X borrar B menu");
    } else if (mode == M_TASK) {
        int t = today();
        iprintf(" TAREAS\n\n");
        for (i = 0; i < NTASK; i++) {
            int dd = TE[i] - (t - S.last[i]);
            char st[24];
            if (dd < 0) snprintf(st, sizeof(st), "VENC %dd", -dd);
            else if (dd == 0) snprintf(st, sizeof(st), "HOY");
            else snprintf(st, sizeof(st), "en %dd", dd);
            iprintf("%c%-21.21s %-8.8s\n", i == kcur ? '>' : ' ', TN[i], st);
        }
        iprintf("\n Comidas: 09:00 y 19:00\n Cada tarea: cada %d dias\n", TE[kcur]);
        foot("A hecha  B menu");
    } else {
        iprintf(" AYUDA\n\n");
        iprintf(" Arriba: tu acuario en vivo.\n Abajo: menus.\n\n");
        iprintf(" Cruceta  mover / cambiar\n A  elegir      B  atras\n X  anadir      Y  quitar\n L/R  tipo de especie\n\n");
        iprintf(" Guardado en la tarjeta SD\n solo si hay (reefkeeper.sav).\n Estado: %s\n\n", haveFat ? "GUARDANDO" : "sin tarjeta");
        iprintf(" Datos orientativos.\n");
        foot("B menu");
    }
}

/* ---------- entrada ---------- */
static void say(const char *s) { strncpy(msg, s, 31); msg[31] = 0; msgt = 150; }
static void handle(u32 d, u32 rep, u32 held) {
    if (mode == M_MENU) {
        if (rep & KEY_UP) { mcur = (mcur + 5) % 6; dirty = 1; }
        if (rep & KEY_DOWN) { mcur = (mcur + 1) % 6; dirty = 1; }
        if (d & KEY_A) {
            dirty = 1;
            if (mcur == 0) { mode = M_CAT; buildFilter(); }
            else if (mcur == 1) { mode = M_TANK; tcur = 0; }
            else if (mcur == 2) { mode = M_VOL; }
            else if (mcur == 3) { mode = M_PAR; pedit = 0; }
            else if (mcur == 4) { mode = M_TASK; }
            else mode = M_HELP;
        }
    } else if (mode == M_CAT) {
        if (rep & KEY_UP) { if (nf) ccur = (ccur + nf - 1) % nf; dirty = 1; }
        if (rep & KEY_DOWN) { if (nf) ccur = (ccur + 1) % nf; dirty = 1; }
        if (d & KEY_L) { cat = (cat + 3) % 4; ccur = 0; coff = 0; buildFilter(); dirty = 1; }
        if (d & KEY_R) { cat = (cat + 1) % 4; ccur = 0; coff = 0; buildFilter(); dirty = 1; }
        if (ccur < coff) coff = ccur;
        if (ccur >= coff + PAGE) coff = ccur - PAGE + 1;
        if ((d & KEY_A) && nf) { dsp = fidx[ccur]; dret = M_CAT; buildDetail(dsp); mode = M_DET; dirty = 1; }
        if (d & KEY_B) { mode = M_MENU; dirty = 1; }
    } else if (mode == M_DET) {
        if ((rep & KEY_UP) && doff > 0) { doff--; dirty = 1; }
        if ((rep & KEY_DOWN) && doff + 22 < nL) { doff++; dirty = 1; }
        if (d & KEY_X) {
            if (S.ntank < MAXT) { S.tank[S.ntank++] = dsp; save(); say("Anadido a tu acuario"); buildDetail(dsp); }
            else say("Acuario lleno");
            dirty = 1;
        }
        if (d & KEY_B) { mode = dret; dirty = 1; }
    } else if (mode == M_TANK) {
        if (rep & KEY_UP) { if (S.ntank) tcur = (tcur + S.ntank - 1) % S.ntank; dirty = 1; }
        if (rep & KEY_DOWN) { if (S.ntank) tcur = (tcur + 1) % S.ntank; dirty = 1; }
        if ((d & KEY_A) && S.ntank) { dsp = S.tank[tcur]; dret = M_TANK; buildDetail(dsp); mode = M_DET; dirty = 1; }
        if ((d & KEY_Y) && S.ntank) {
            int i;
            for (i = tcur; i < S.ntank - 1; i++) S.tank[i] = S.tank[i + 1];
            S.ntank--; if (tcur >= S.ntank) tcur = S.ntank ? S.ntank - 1 : 0;
            save(); say("Quitado"); dirty = 1;
        }
        if (d & KEY_B) { mode = M_MENU; dirty = 1; }
    } else if (mode == M_VOL) {
        int st = (held & KEY_R) ? 10 : 1;
        if (rep & KEY_LEFT) { vfield = (vfield + 7) % 8; dirty = 1; }
        if (rep & KEY_RIGHT) { vfield = (vfield + 1) % 8; dirty = 1; }
        if (rep & KEY_UP) { S.dim[vfield] += st; if (S.dim[vfield] > 9999) S.dim[vfield] = 9999; save(); dirty = 1; }
        if (rep & KEY_DOWN) { S.dim[vfield] -= st; if (S.dim[vfield] < 0) S.dim[vfield] = 0; save(); dirty = 1; }
        if (d & KEY_B) { mode = M_MENU; dirty = 1; }
    } else if (mode == M_PAR) {
        int st = P[pcur].step * ((held & KEY_R) ? 10 : 1);
        if (!pedit) {
            if (rep & KEY_UP) { pcur = (pcur + NPAR - 1) % NPAR; dirty = 1; }
            if (rep & KEY_DOWN) { pcur = (pcur + 1) % NPAR; dirty = 1; }
            if (d & KEY_A) { pedit = 1; if (!S.pset[pcur]) { S.pset[pcur] = 1; S.pv[pcur] = P[pcur].def; } dirty = 1; }
            if (d & KEY_X) { S.pset[pcur] = 0; save(); dirty = 1; }
            if (d & KEY_B) { mode = M_MENU; dirty = 1; }
        } else {
            if (rep & KEY_UP) { S.pv[pcur] += st; dirty = 1; }
            if (rep & KEY_DOWN) { S.pv[pcur] -= st; if (S.pv[pcur] < 0) S.pv[pcur] = 0; dirty = 1; }
            if ((d & KEY_A) || (d & KEY_B)) { pedit = 0; save(); dirty = 1; }
        }
    } else if (mode == M_TASK) {
        if (rep & KEY_UP) { kcur = (kcur + NTASK - 1) % NTASK; dirty = 1; }
        if (rep & KEY_DOWN) { kcur = (kcur + 1) % NTASK; dirty = 1; }
        if (d & KEY_A) { S.last[kcur] = today(); save(); say("Tarea hecha"); dirty = 1; }
        if (d & KEY_B) { mode = M_MENU; dirty = 1; }
    } else {
        if (d & KEY_B) { mode = M_MENU; dirty = 1; }
    }
}

int main(void) {
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&topc, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&botc, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
#ifdef USE_FAT
    haveFat = fatInitDefault() ? 1 : 0;
#endif
    keysSetRepeat(15, 4);
    load();
    drawTop();
    while (1) {
        u32 d, rep, held;
        scanKeys();
        d = keysDown();
        rep = keysDownRepeat();
        held = keysHeld();
        handle(d, rep, held);
        if (msgt > 0) { msgt--; if (msgt == 0) dirty = 1; }
        if (dirty) { drawBottom(); dirty = 0; }
        if (frame % 8 == 0) drawTop();
        frame++;
        swiWaitForVBlank();
    }
    return 0;
}
