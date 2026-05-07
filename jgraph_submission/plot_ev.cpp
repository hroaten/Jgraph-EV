// plot_ev.cpp — read a *_base.bj EV table and emit a jgraph script
// that lays out a basic-strategy chart with a TC-vs-EV mini-plot per cell.
//
// Usage:
//   ./plot_ev <ruleset_name> <hard|soft|pairs> <lines|optimal>  > out.jgr
//
// Then render with:  jgraph -P out.jgr | ps2pdf - out.pdf
//                    convert -density 200 out.pdf out.jpg

#include "bj.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static constexpr uint32_t EV_MAGIC = 0x424A4556;
struct EVHeader {
    uint32_t magic;
    uint32_t entry_size;
    uint32_t total_entries;
};


// Layout sized to fit a landscape letter page (11" x 8.5") after rendering
// with `jgraph -L`. Total chart width ~9.5", height ~7.0", leaving room for
// title at top and legend on the right.
static constexpr double CELL_W = 0.78;
static constexpr double CELL_H = 0.33;
static constexpr double CELL_PAD_X = 0.10;
static constexpr double CELL_PAD_Y = 0.07;

// LEFT_MARGIN = x-coord of left edge of chart cells (room for row labels).
// BOTTOM_MARGIN = y-coord of bottom edge of chart (room for legend below).
static constexpr double LEFT_MARGIN   = 0.70;
static constexpr double BOTTOM_MARGIN = 0.80;

// Per-cell y-axis ranges are computed on the fly from the cell's actual
// EV data so that variation across true count is visible. (Each cell scales
// independently — the visual signal is "how does this move's EV change with
// TC", not absolute magnitude across cells.)

struct RGB { double r, g, b; };
static constexpr RGB MOVE_COLORS[4] = {
    {0.20, 0.50, 0.20}, // STAND  - green
    {0.85, 0.20, 0.20}, // HIT    - red
    {0.20, 0.30, 0.85}, // DOUBLE - blue
    {0.85, 0.55, 0.10}, // SPLIT  - orange
};
static const char* MOVE_NAMES[4] = {"Stand", "Hit", "Double", "Split"};

struct HandRow { HandCat hc; const char* label; };

static vector<HandRow> hard_hands() {
    vector<HandRow> v;
    static const char* names[] = {
        "5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20"
    };
    int idx = 0;
    for (int t = 5; t <= 20; ++t) {
        HandCat hc = (HandCat)((int)HandCat::HARD_4 + t - 4);
        v.push_back({hc, names[idx++]});
    }
    return v;
}

static vector<HandRow> soft_hands() {
    vector<HandRow> v;
    static const char* names[] = {
        "A,2","A,3","A,4","A,5","A,6","A,7","A,8","A,9"
    };
    int idx = 0;
    for (int t = 13; t <= 20; ++t) {
        HandCat hc = (HandCat)((int)HandCat::SOFT_13 + t - 13);
        v.push_back({hc, names[idx++]});
    }
    return v;
}

static vector<HandRow> pair_hands() {
    vector<HandRow> v;
    static const char* names[] = {
        "2,2","3,3","4,4","5,5","6,6","7,7","8,8","9,9","T,T","A,A"
    };
    for (int r = 0; r < RANKS; ++r) {
        HandCat hc = (HandCat)((int)HandCat::PAIR_2 + r);
        v.push_back({hc, names[r]});
    }
    return v;
}

// Dealer-up column labels (2..A, mapping to rank index 0..9)
static const char* DEALER_LABELS[RANKS] = {
    "2","3","4","5","6","7","8","9","T","A"
};


static vector<ev_entry> load_table(const string& ruleset, const string& kind) {
    string path = "./EV/" + ruleset + (kind == "base" ? "_base.bj" : ".bj");

    FILE* f = fopen(path.c_str(), "rb");

    if (!f) {
        fprintf(stderr, "Failed to open %s\n", path.c_str());
        exit(1);
    }
    EVHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) {
        fprintf(stderr, "Failed reading header\n"); exit(1);
    }
    if (h.magic != EV_MAGIC) {
        fprintf(stderr, "Bad magic 0x%08x\n", h.magic); exit(1);
    }
    if (h.entry_size != sizeof(ev_entry)) {
        fprintf(stderr, "entry_size mismatch: file=%u runtime=%zu\n",
                h.entry_size, sizeof(ev_entry));
        exit(1);
    }
    vector<ev_entry> entries(h.total_entries);
    if (fread(entries.data(), sizeof(ev_entry), h.total_entries, f) != h.total_entries) {
        fprintf(stderr, "Short read on entries\n"); exit(1);
    }
    fclose(f);
    return entries;
}

static inline const ev_entry& at(const vector<ev_entry>& E, HandCat hc, int dealer_up, int tc_bucket) {
    int h = (int)hc;
    return E[(h * RANKS + dealer_up) * NUM_TC_BUCKETS + tc_bucket];
}

static double get_ev(const ev_entry& e, int move) {
    double w = e.ev_weight[move];
    if (w <= 0.0) return numeric_limits<double>::quiet_NaN();
    return e.ev_sum[move] / w;
}

static inline double bucket_tc(int b) {
    return TC_MIN + b * TC_BUCKET_SIZE;
}

static int optimal_move(const vector<ev_entry>& E, HandCat hc, int dealer_up, int b) {
    const ev_entry& e = at(E, hc, dealer_up, b);
    int best = -1; double best_ev = -1e300;
    for (int m = 0; m < 4; ++m) {
        double w = e.ev_weight[m];
        if (w <= 0.0) continue;
        double ev = e.ev_sum[m] / w;
        if (ev > best_ev) { best_ev = ev; best = m; }
    }
    return best;
}

// Compute the y-axis range for a single cell. Range is symmetric around 0
// so that the EV=0 axis line sits at the vertical center of every cell —
// this keeps the (TC=0, EV=0) crosshair at the geometric center, which makes
// cells line up cleanly along rows and columns. The range itself auto-scales
// to the cell's data so EV variation is still visible.
static void compute_cell_yrange(const vector<ev_entry>& E,
                                 HandCat hc, int dealer_up,
                                 double& ymin, double& ymax) {
    double max_abs = 0.0;
    bool any = false;
    for (int b = 0; b < NUM_TC_BUCKETS; ++b) {
        const ev_entry& e = at(E, hc, dealer_up, b);
        for (int m = 0; m < 4; ++m) {
            double ev = get_ev(e, m);
            if (isnan(ev) || isinf(ev)) continue;
            double a = ev < 0 ? -ev : ev;
            if (a > max_abs) max_abs = a;
            any = true;
        }
    }
    if (!any || max_abs == 0.0) max_abs = 0.1;     // tiny default range
    max_abs *= 1.08;                                // 8% margin
    ymin = -max_abs;
    ymax = +max_abs;
}

// Common per-cell graph header: position, sizes, axes drawn through zero with
// no hash marks or tick labels (a clean crosshair). The axis *lines* still
// render because we don't pass `nodraw`.
static void emit_cell_header(FILE* out, double tx, double ty,
                              double ymin, double ymax) {
    fprintf(out, "newgraph\n");
    fprintf(out, "  x_translate %.3f y_translate %.3f\n", tx, ty);
    fprintf(out,
        "  xaxis size %.3f min %.2f max %.2f draw_at 0"
        " no_draw_hash_marks no_draw_hash_labels mhash 0\n",
        CELL_W, TC_MIN, TC_MAX);
    fprintf(out,
        "  yaxis size %.3f min %.4f max %.4f draw_at 0"
        " no_draw_hash_marks no_draw_hash_labels mhash 0\n",
        CELL_H, ymin, ymax);
}

// Mode "lines": Plot each move as its own curve
static void emit_cell_lines(FILE* out, const vector<ev_entry>& E,HandCat hc, int dealer_up, double tx, double ty) {
    double ymin, ymax;
    compute_cell_yrange(E, hc, dealer_up, ymin, ymax);
    emit_cell_header(out, tx, ty, ymin, ymax);

    for (int m = 0; m < 4; ++m) {
        // Collect contiguous segments where this move has data and is finite
        bool any = false;
        string pts;
        char buf[64];
        for (int b = 0; b < NUM_TC_BUCKETS; ++b) {
            double ev = get_ev(at(E, hc, dealer_up, b), m);
            if (isnan(ev) || isinf(ev)) continue;
            snprintf(buf, sizeof(buf), " %.3f %.4f", bucket_tc(b), ev);
            pts += buf;
            any = true;
        }
        if (!any) continue;
        const RGB& c = MOVE_COLORS[m];
        fprintf(out,
            "  newcurve marktype none linetype solid color %.2f %.2f %.2f pts%s\n",
            c.r, c.g, c.b, pts.c_str());
    }
}
// Mode "optimal": Plot optimal move, color changes indicate strategy deviation
static void emit_cell_optimal(FILE* out, const vector<ev_entry>& E, HandCat hc, int dealer_up, double tx, double ty) {
    double ymin, ymax;
    compute_cell_yrange(E, hc, dealer_up, ymin, ymax);
    emit_cell_header(out, tx, ty, ymin, ymax);

    // For each bucket, compute (tc, ev_of_optimal, opt_move)
    struct Pt { double tc; double ev; int move; };
    vector<Pt> pts;
    pts.reserve(NUM_TC_BUCKETS);
    for (int b = 0; b < NUM_TC_BUCKETS; ++b) {
        int m = optimal_move(E, hc, dealer_up, b);
        if (m < 0) continue;
        double ev = get_ev(at(E, hc, dealer_up, b), m);
        pts.push_back({bucket_tc(b), ev, m});
    }
    if (pts.empty()) return;

    //change color with shifts in optimal move 
    vector<string> per_move_pts(4);
    auto append_seg = [&](int move, double x1, double y1, double x2, double y2) {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "  newcurve marktype none linetype solid color %.2f %.2f %.2f pts %.3f %.4f %.3f %.4f\n",
            MOVE_COLORS[move].r, MOVE_COLORS[move].g, MOVE_COLORS[move].b,
            x1, y1, x2, y2);
        per_move_pts[move] += buf;
    };

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Pt& a = pts[i];
        const Pt& b = pts[i+1];
        if (a.move == b.move) {
            append_seg(a.move, a.tc, a.ev, b.tc, b.ev);
        } else {
            double mx = 0.5 * (a.tc + b.tc);
            double my = 0.5 * (a.ev + b.ev);
            append_seg(a.move, a.tc, a.ev, mx, my);
            append_seg(b.move, mx, my,    b.tc, b.ev);
        }
    }

    for (int m = 0; m < 4; ++m) {
        if (!per_move_pts[m].empty()) {
            fprintf(out, "%s", per_move_pts[m].c_str());
        }
    }
}

static void emit_label_graph(FILE* out,
                              const vector<HandRow>& rows,
                              double total_w_per_cell, double total_h_per_cell,
                              double chart_y_top,
                              double page_w, double page_h,
                              const string& title,
                              bool optimal_mode) {
    fprintf(out, "newgraph\n");
    fprintf(out, "  x_translate 0 y_translate 0\n");
    fprintf(out, "  xaxis size %.3f min 0 max %.3f nodraw\n", page_w, page_w);
    fprintf(out, "  yaxis size %.3f min 0 max %.3f nodraw\n", page_h, page_h);

    // Title
    fprintf(out,
        "  newstring x %.3f y %.3f hjc vjb fontsize 14 font Times-Bold : %s\n",
        page_w * 0.5, chart_y_top + 0.30, title.c_str());
        
    // label dealer up cards — centered horizontally on the cell (not cell+pad)
    for (int d = 0; d < RANKS; ++d) {
        double cx = LEFT_MARGIN + d * total_w_per_cell + CELL_W * 0.5;
        double cy = chart_y_top + 0.05;
        fprintf(out,
            "  newstring x %.3f y %.3f hjc vjb fontsize 11 font Times-Bold : %s\n",
            cx, cy, DEALER_LABELS[d]);
    }
    // label player hands — centered vertically on the cell (not cell+pad)
    for (size_t r = 0; r < rows.size(); ++r) {
        double rx = LEFT_MARGIN - 0.10;
        double ry = chart_y_top - (double)(r + 1) * total_h_per_cell + CELL_H * 0.5;
        fprintf(out,
            "  newstring x %.3f y %.3f hjr vjc fontsize 10 font Times-Bold : %s\n",
            rx, ry, rows[r].label);
    }

    // Legend — placed below the chart, centered horizontally. Each entry is
    // a colored line + label; layout is horizontal (4 entries side by side).
    double chart_bottom = chart_y_top - rows.size() * total_h_per_cell;
    double chart_w = LEFT_MARGIN + RANKS * total_w_per_cell;
    double ly = chart_bottom - 0.45;
    const char* mode_text = optimal_mode ? "Color = optimal move" : "Move EV curves";
    fprintf(out,
        "  newstring x %.3f y %.3f hjc vjb fontsize 10 font Times-Bold : %s\n",
        chart_w * 0.5, ly + 0.20, mode_text);

    // Lay 4 entries across, evenly spaced
    double entry_w = 1.4;
    double total_legend_w = entry_w * 4;
    double lx0 = (chart_w - total_legend_w) * 0.5;
    for (int m = 0; m < 4; ++m) {
        double lx = lx0 + m * entry_w;
        const RGB& c = MOVE_COLORS[m];
        fprintf(out,
            "  newcurve marktype none linetype solid color %.2f %.2f %.2f pts %.3f %.3f %.3f %.3f\n",
            c.r, c.g, c.b, lx, ly, lx + 0.30, ly);
        fprintf(out,
            "  newstring x %.3f y %.3f hjl vjc fontsize 9 : %s\n",
            lx + 0.35, ly, MOVE_NAMES[m]);
    }
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s <ruleset> <hard|soft|pairs> <lines|optimal> <base|cumulative>\n",
            argv[0]);
        return 1;
    }
    string ruleset = argv[1];
    string chart   = argv[2];
    string mode    = argv[3];
    string source  = argv[4];

    bool optimal_mode;
    if      (mode == "lines")   optimal_mode = false;
    else if (mode == "optimal") optimal_mode = true;
    else {fprintf(stderr, "mode must be lines or optimal\n"); return 1;}

    if (source != "base" && source != "cumulative") {
        fprintf(stderr, "source must be base or cumulative\n");
        return 1;
    }

    vector<HandRow> rows;
    string title_suffix;
    if      (chart == "hard")  { rows = hard_hands();  title_suffix = "Hard Totals"; }
    else if (chart == "soft")  { rows = soft_hands();  title_suffix = "Soft Totals"; }
    else if (chart == "pairs") { rows = pair_hands();  title_suffix = "Pairs"; }
    else { fprintf(stderr, "chart must be hard, soft, or pairs\n"); return 1; }

    auto entries = load_table(ruleset, source);

    FILE* out = stdout;

    double total_w = CELL_W + CELL_PAD_X;
    double total_h = CELL_H + CELL_PAD_Y;
    double chart_w = LEFT_MARGIN + RANKS * total_w;
    double chart_h_top = BOTTOM_MARGIN + rows.size() * total_h;

    double page_w = chart_w + 0.30;
    double page_h = chart_h_top + 0.60;  // 0.6" of headroom for the title

    fprintf(out, "(* %s ruleset=%s mode=%s source=%s *)\n",
            title_suffix.c_str(), ruleset.c_str(), mode.c_str(), source.c_str());

    string source_tag = (source == "base") ? "base" : "cumulative";
    string title = "Blackjack EV vs True Count - " + title_suffix + " (" + ruleset + ", " + source_tag + ")";

    for (size_t r = 0; r < rows.size(); ++r) {
        for (int d = 0; d < RANKS; ++d) {
            double tx = LEFT_MARGIN + d * total_w;
            double ty = chart_h_top - (double)(r + 1) * total_h;

            if(optimal_mode) emit_cell_optimal(out, entries, rows[r].hc, d, tx, ty);
            else             emit_cell_lines  (out, entries, rows[r].hc, d, tx, ty);
        }
    }

    emit_label_graph(out, rows, total_w, total_h, chart_h_top,
                     page_w, page_h, title, optimal_mode);

    return 0;
}