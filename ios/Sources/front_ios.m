// iOS front end (implements front.h): the Metal view the game presents to, letterboxed to the game picture's aspect ratio; touches → dm_touch_set,
// or on-screen controls for a game steered with a controller (game.json touch "pad", host/front/touchpad.c); game controllers; the software keyboard
// dialog; scene lifecycle, audio session and thermal logging. main.m builds the window and forwards the scene events to front_ios_focus_event.
#import <UIKit/UIKit.h>
#import <UIKit/UIGestureRecognizerSubclass.h>   // DmAnyTouch sets its own state
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <signal.h>
#include <stdatomic.h>
#include "cloud.h"
#include "dm_input.h"
#include "front.h"
#include "game.h"
#include "native_size.h"
#include "hle.h"
#include "touchpad.h"

int mtl_fps_target(void);   // gfx/nvn/nvn_mtl.m: 60, or 30 in the power-saving mode
static CAMetalLayer *g_layer;

// Game picture size: DM_GAME_RES=<w>x<h>, else the profile's screen size. The presenter keeps this aspect ratio and centers the picture; the game view
// below is laid out the same way, so normalized touch coordinates match the picture
static void game_size(double *w, double *h) {
    static double gw, gh;
    if (!gw) {
        unsigned ew, eh; const char *e = getenv("DM_GAME_RES");
        if (e && sscanf(e, "%ux%u", &ew, &eh) == 2 && ew && eh) { gw = ew; gh = eh; }
        else { gw = dm_game.screen_w > 0 ? dm_game.screen_w : 1280; gh = dm_game.screen_h > 0 ? dm_game.screen_h : 720; }
    }
    *w = gw; *h = gh;
}
// The largest rectangle of the game's aspect ratio centered in f (a difference under 1% fills f)
static CGRect letterbox_rect(CGRect f) {
    double gw, gh; game_size(&gw, &gh);
    if (f.size.width <= 0 || f.size.height <= 0) return f;
    double ga = gw / gh, wa = f.size.width / f.size.height;
    if (fabs(wa / ga - 1) < 0.01) return f;
    CGFloat w = f.size.width, h = f.size.height;
    if (wa > ga) w = floor(h * ga); else h = floor(w / ga);
    return CGRectMake(f.origin.x + floor((f.size.width - w) / 2), f.origin.y + floor((f.size.height - h) / 2), w, h);
}
// The screen the game's window is on (plan item 302: the iPhone Duo has two, the outer and the inner display): its maximum refresh rate, kept on
// the main thread for mtl_display_hz (gfx/nvn/mtl/texdump.inc, read on the game thread), which read UIScreen.mainScreen before
static _Atomic int g_screen_hz;
int front_ios_screen_hz(void) { return atomic_load(&g_screen_hz); }
static void screen_note(UIScreen *s) { if (s) atomic_store(&g_screen_hz, (int)s.maximumFramesPerSecond); }
// The setting "Native" (plan item 302): the screen the game's window is on is reported to the game when it changes (the iPhone Duo opened or
// closed moves the window between its displays; games/manosaba/graphics.c renders at that screen's size from a later frame). The largest screen
// seen is kept as dm.native_screen_max for the next launch's window textures (ios/Sources/main.m)
static int g_follow; static unsigned g_follow_w, g_follow_h;
static void native_remember(unsigned w, unsigned h) {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults; NSString *mx = [d stringForKey:@"dm.native_screen_max"]; unsigned a, b, W, H, MW, MH;
    if (!dm_native_size(w, h, &W, &H)) return;
    if (mx && sscanf(mx.UTF8String, "%ux%u", &a, &b) == 2 && dm_native_size(a, b, &MW, &MH) && MH >= H) return;
    [d setObject:[NSString stringWithFormat:@"%ux%u", w, h] forKey:@"dm.native_screen_max"];
}
void front_ios_native_follow(unsigned w, unsigned h) { g_follow = 1; g_follow_w = w; g_follow_h = h; native_remember(w, h); }   // main thread
static void native_follow(UIScreen *s) {
    unsigned w = (unsigned)s.nativeBounds.size.width, h = (unsigned)s.nativeBounds.size.height;
    if (!g_follow || !w || !h || (w == g_follow_w && h == g_follow_h)) return;
    g_follow_w = w; g_follow_h = h; native_remember(w, h);
    LOG("front: the window is now on a screen of %ux%u pixels: reported to the game (Native)", w, h);
    dm_game_native_screen(w, h);
}
// The window, its screen and the game view, logged when they change (the iPhone Duo opened or closed, a turn), a hundred times at most. The
// interface orientation is only logged: the side its safe area insets name is the side of the outer display's front camera (plan item 302)
static void layout_note(UIView *v, CGRect g) {
    static char last[512]; static int logged; if (logged >= 100) return;
    UIWindowScene *ws = v.window.windowScene; UIScreen *s = ws.screen; UIEdgeInsets in = v.safeAreaInsets; CGSize b = v.bounds.size; long ori = 0; char line[512];
    if (@available(iOS 26.0, *)) ori = (long)ws.effectiveGeometry.interfaceOrientation;
    snprintf(line, sizeof line, "window %.0fx%.0f on a screen of %.0fx%.0f pixels up to %ld Hz (interface orientation %ld, safe area top %.0f left %.0f bottom %.0f right %.0f); "
             "game view %.0fx%.0f at (%.0f, %.0f)", b.width, b.height, s.nativeBounds.size.width, s.nativeBounds.size.height, (long)s.maximumFramesPerSecond, ori,
             in.top, in.left, in.bottom, in.right, g.size.width, g.size.height, g.origin.x, g.origin.y);
    if (strcmp(line, last)) { strlcpy(last, line, sizeof last); logged++; LOG("front: %s", line); }
}

// The drawable's size: the view's pixels, or with DM_IOS_DRAWABLE_H=<rows> that many rows at the view's shape, so the system compositor scales
// the layer to the screen and our GPU fills fewer pixels a frame (MacNativeGames' TOTK_IOS_DRAWABLE_H, MIT, written anew; kxn/d2r-mobile measured
// 9.5 % fewer GPU writes on an Adreno 735 with its buffer at the game's size; plan item 270, an A/B on the phone)
static CGSize drawable_size(CGSize pt, CGFloat scale) {
    static int rows = -1; if (rows < 0) { const char *e = getenv("DM_IOS_DRAWABLE_H"); rows = e ? atoi(e) : 0; if (rows < 0) rows = 0;
        if (rows) LOG("front: drawable %d rows at the view's shape (DM_IOS_DRAWABLE_H), the compositor scales it", rows); }
    if (!rows || pt.height <= 0 || rows >= pt.height * scale) return CGSizeMake(pt.width * scale, pt.height * scale);
    return CGSizeMake(round(pt.width * rows / pt.height), rows);
}

// ---------- Game view: CAMetalLayer + touches ----------
// Touches: every finger gets the lowest free slot 0–15 on touch down and keeps it until it lifts; the whole set goes to the host in coordinates
// normalized to this view on every change (hle_touch.c scales them to the game's touch space)
@interface DmMetalView : UIView
- (void)liftAll;
@end
@implementation DmMetalView { UITouch *_slot[16]; }
+ (Class)layerClass { return CAMetalLayer.class; }
- (instancetype)initWithFrame:(CGRect)f {
    if (!(self = [super initWithFrame:f])) return nil;
    self.multipleTouchEnabled = YES;
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CAMetalLayer *l = (CAMetalLayer *)self.layer;
    l.drawableSize = drawable_size(self.bounds.size, l.contentsScale);
}
- (void)push {
    dm_touch pt[16]; int n = 0; CGSize b = self.bounds.size;
    if (b.width > 0 && b.height > 0)
        for (int i = 0; i < 16; i++) if (_slot[i]) {
            CGPoint p = [_slot[i] locationInView:self];
            pt[n++] = (dm_touch){(uint32_t)i, (float)fmin(fmax(p.x / b.width, 0), .99999), (float)fmin(fmax(p.y / b.height, 0), .99999)};
        }
    dm_touch_set(pt, n);
}
- (void)touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e {
    (void)e;
    for (UITouch *t in ts) for (int i = 0; i < 16; i++) if (!_slot[i]) { _slot[i] = t; break; }
    [self push];
}
- (void)touchesMoved:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)ts; (void)e; [self push]; }
- (void)lift:(NSSet<UITouch *> *)ts { for (UITouch *t in ts) for (int i = 0; i < 16; i++) if (_slot[i] == t) _slot[i] = nil; [self push]; }
- (void)touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self lift:ts]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self lift:ts]; }
- (void)liftAll { for (int i = 0; i < 16; i++) _slot[i] = nil; dm_touch_set(NULL, 0); }
@end
static __weak DmMetalView *g_view;

// Black container that keeps the game view at the game's aspect ratio, centered. Laid out again when its size changes: the iPhone Duo opened or
// closed moves the window between its displays (plan item 302). The picture is not moved off the outer display's front camera: its cutout covers
// a small part of one corner of the picture, and keeping clear of it would cost more of the picture (the user, 2026-11-01); the device reports no
// camera region to lay out around (reservedRegionsOfKind: empty on iOS 27.1 24A94425, while its Simulator reports the corner)
@interface DmLetterboxView : UIView
@property (nonatomic, strong) UIView *game;
@end
@implementation DmLetterboxView
- (void)layoutSubviews {
    [super layoutSubviews]; UIScreen *s = self.window.windowScene.screen; screen_note(s); if (s) native_follow(s);
    if (self.bounds.size.width > 0 && self.bounds.size.height > 0) { self.game.frame = letterbox_rect(self.bounds); layout_note(self, self.game.frame); }
}
@end

// ---------- On-screen controls (host/front/touchpad.c) ----------
// Over the whole screen (the letterbox bars too), for a game with game.json touch "pad" (DM_TOUCH=pad / screen overrides it): the controls are laid
// out inside the safe area, every finger goes to tp_touch, drawn from tp_draw after each touch. Off while a game controller with an extended
// profile is connected (BotWRecomp's automatic mode): the view is hidden and touches reach the game view. The eye's choice is kept in the user
// defaults (dm.touch_hidden). The camera finger's sensitivity and inversion come from the Settings page (dm.look_sensitivity, dm.look_invert_x /
// _y; tp_look_tuning, plan item 109), applied at start unless DM_TOUCH_LOOK was set, and whenever they change.
static float g_look_s = -1; static int g_look_x, g_look_y;   // the Settings values as last read
static void pad_look_settings(void) {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    float s = [d floatForKey:@"dm.look_sensitivity"]; int x = [d boolForKey:@"dm.look_invert_x"], y = [d boolForKey:@"dm.look_invert_y"];
    if (s == g_look_s && x == g_look_x && y == g_look_y) return;   // another setting changed (the eye's, for one): DM_TOUCH_LOOK stays
    int first = g_look_s < 0; g_look_s = s; g_look_x = x; g_look_y = y;
    if (first && getenv("DM_TOUCH_LOOK")) return;
    tp_look_tuning(s, x, y); float a; int ax, ay; tp_look_tuning_get(&a, &ax, &ay);
    LOG("front: touch camera sensitivity %.2f×%s%s (Settings)", a, ax ? ", horizontal inverted" : "", ay ? ", vertical inverted" : "");
}
// The controls' layout and opacity (plan item 148): dm.touch_layout (tp_custom_get's spec, written by the layout editor) and dm.touch_opacity (0.15-1,
// the panel or the Settings page), applied at start and whenever they change
static NSString *g_layout_spec; static float g_pad_alpha = -1;
static void pad_layout_settings(void) {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults; NSString *spec = [d stringForKey:@"dm.touch_layout"] ?: @""; float a = [d floatForKey:@"dm.touch_opacity"];
    if (![spec isEqualToString:g_layout_spec ?: @"-"]) { g_layout_spec = spec; if (!tp_custom_set(spec.UTF8String)) LOG("[red] front: dm.touch_layout does not parse: %s", spec.UTF8String); }
    if (a != g_pad_alpha) { g_pad_alpha = a; tp_opacity(a > 0 ? a : 1); }
}
static void front_panel_open(void);
static UIViewController *top_controller(void);
@class DmPadView;
@interface DmEditBar : UIView
- (instancetype)initWithPad:(DmPadView *)pad;
- (void)selectionChanged;
@end
@interface DmPadView : UIView
- (void)startEdit;
- (void)endEdit:(BOOL)save;
@end
@implementation DmPadView { int _hidden; NSTimer *_test; DmEditBar *_bar; }
- (instancetype)initWithFrame:(CGRect)f {
    if (!(self = [super initWithFrame:f])) return nil;
    self.multipleTouchEnabled = YES; self.opaque = NO; self.backgroundColor = UIColor.clearColor; self.contentMode = UIViewContentModeRedraw;
    _hidden = [NSUserDefaults.standardUserDefaults boolForKey:@"dm.touch_hidden"]; tp_set_hidden(_hidden);
    pad_look_settings(); pad_layout_settings();
    [NSNotificationCenter.defaultCenter addObserverForName:NSUserDefaultsDidChangeNotification object:nil queue:NSOperationQueue.mainQueue
                                                usingBlock:^(NSNotification *n) { (void)n; pad_look_settings(); pad_layout_settings(); [self setNeedsDisplay]; }];
    if (getenv("DM_TEST_TOUCHPAD")) _test = [NSTimer scheduledTimerWithTimeInterval:1 / 15.0 repeats:YES block:^(NSTimer *t) { (void)t; [self setNeedsDisplay]; }];   // the self-test's fingers
    return self;
}
- (void)relayout {
    UIEdgeInsets in = self.safeAreaInsets; CGSize b = self.bounds.size; float wt = 0;
    if (@available(iOS 26.0, *)) {   // the iPadOS window controls in the top-left corner (plan item 171, as Touchable reads them): the left column goes below
        UIViewLayoutRegion *rg = [UIViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:UIViewLayoutRegionAdaptivityAxisVertical];
        wt = (float)[self edgeInsetsForLayoutRegion:rg].top; }
    tp_layout((float)b.width, (float)b.height, (float)in.left, (float)in.top, (float)in.right, (float)in.bottom, wt);
    [self setNeedsDisplay];
}
- (void)layoutSubviews { [super layoutSubviews]; [self relayout]; }
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange]; [self relayout]; }
- (void)send:(NSSet<UITouch *> *)ts phase:(int)ph {
    for (UITouch *t in ts) { CGPoint p = [t locationInView:self]; tp_touch((long)(__bridge void *)t, ph, (float)p.x, (float)p.y); }
    int h = tp_hidden();
    if (h != _hidden) { _hidden = h; [NSUserDefaults.standardUserDefaults setBool:h forKey:@"dm.touch_hidden"]; }
    if (tp_menu_take()) front_panel_open();
    [_bar selectionChanged];
    [self setNeedsDisplay];
}
- (void)touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self send:ts phase:TP_DOWN]; }
- (void)touchesMoved:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self send:ts phase:TP_MOVE]; }
- (void)touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self send:ts phase:TP_UP]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self send:ts phase:TP_UP]; }
static void pad_label(const char *s, CGFloat x, CGFloat y, CGFloat size, CGFloat alpha) {
    NSDictionary *a = @{NSFontAttributeName: [UIFont systemFontOfSize:size weight:UIFontWeightSemibold], NSForegroundColorAttributeName: [UIColor colorWithWhite:1 alpha:alpha]};
    NSString *t = @(s); CGSize z = [t sizeWithAttributes:a];
    [t drawAtPoint:CGPointMake(x - z.width / 2, y - z.height / 2) withAttributes:a];
}
- (void)drawRect:(CGRect)r {
    (void)r; tp_draw_state d; tp_draw(&d);
    if (!d.visible) return;
    CGFloat A = d.opacity > 0 ? d.opacity : 1;   // plan item 148: the controls' opacity
    UIColor *line = [UIColor colorWithWhite:1 alpha:0.45 * A], *fill = [UIColor colorWithWhite:0 alpha:0.18 * A], *down = [UIColor colorWithWhite:1 alpha:0.35 * A];
    for (int i = 0; i < d.n; i++) {
        tp_item *it = &d.items[i]; UIBezierPath *p;
        if (it->shape == TP_PILL) p = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(it->x - it->r, it->y - it->hh, 2 * it->r, 2 * it->hh) cornerRadius:it->hh];
        else if (it->shape == TP_DPAD) {   // a cross of three-by-one arms; the held direction filled
            CGFloat a = it->r / 3;
            p = [UIBezierPath bezierPathWithRect:CGRectMake(it->x - a / 2 * 1.4, it->y - it->r, a * 1.4, 2 * it->r)];
            [p appendPath:[UIBezierPath bezierPathWithRect:CGRectMake(it->x - it->r, it->y - a / 2 * 1.4, 2 * it->r, a * 1.4)]];
        } else p = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(it->x - it->r, it->y - it->r, 2 * it->r, 2 * it->r)];
        [(it->pressed && it->shape != TP_DPAD ? down : fill) setFill]; [p fill]; [line setStroke]; p.lineWidth = 1.5; [p stroke];
        if (it->shape == TP_DPAD) {
            static const struct { uint32_t m; float dx, dy; } arm[] = {{DM_BTN_UP, 0, -1}, {DM_BTN_DOWN, 0, 1}, {DM_BTN_LEFT, -1, 0}, {DM_BTN_RIGHT, 1, 0}};
            for (int k = 0; k < 4; k++) if (it->dir & arm[k].m) {
                CGFloat a = it->r / 3 * 1.4;
                [down setFill]; [[UIBezierPath bezierPathWithRect:CGRectMake(it->x + arm[k].dx * it->r * 0.65 - a / 2, it->y + arm[k].dy * it->r * 0.65 - a / 2, a, a)] fill];
            }
        } else if (it->shape == TP_EYE) {   // an eye; struck through while the controls are collapsed
            UIBezierPath *e = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(it->x - it->r * 0.6, it->y - it->r * 0.32, it->r * 1.2, it->r * 0.64)];
            [line setStroke]; [e stroke]; [[UIBezierPath bezierPathWithOvalInRect:CGRectMake(it->x - it->r * 0.16, it->y - it->r * 0.16, it->r * 0.32, it->r * 0.32)] fill];
            if (d.collapsed) { UIBezierPath *s = [UIBezierPath bezierPath]; [s moveToPoint:CGPointMake(it->x - it->r * 0.6, it->y + it->r * 0.6)]; [s addLineToPoint:CGPointMake(it->x + it->r * 0.6, it->y - it->r * 0.6)]; [s stroke]; }
        } else if (it->shape == TP_MENU) {   // three bars: the settings panel
            for (int k = -1; k <= 1; k++) { UIBezierPath *b = [UIBezierPath bezierPath]; [b moveToPoint:CGPointMake(it->x - it->r * 0.5, it->y + k * it->r * 0.32)];
                [b addLineToPoint:CGPointMake(it->x + it->r * 0.5, it->y + k * it->r * 0.32)]; b.lineWidth = 1.5; [line setStroke]; [b stroke]; }
        } else pad_label(it->label, it->x, it->y, it->shape == TP_PILL ? it->hh * 1.0 : it->r * 0.8, (it->pressed ? 1 : 0.8) * A);
        if (d.editing) {   // the layout editor: every control outlined, the one held in yellow
            CGFloat ex = it->r + 3, ey = (it->shape == TP_PILL ? it->hh : it->r) + 3;
            UIBezierPath *o = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(it->x - ex, it->y - ey, 2 * ex, 2 * ey) cornerRadius:6];
            CGFloat dash[2] = {4, 3}; [o setLineDash:dash count:2 phase:0]; o.lineWidth = i == d.sel ? 2.5 : 1;
            [(i == d.sel ? UIColor.systemYellowColor : [UIColor colorWithWhite:1 alpha:0.8]) setStroke]; [o stroke];
        }
    }
    if (d.collapsed) return;
    // the stick: its ring at home while idle, else the base where the thumb is and the knob under it; a small ring under a camera finger
    CGFloat bx = d.stick ? d.bx : d.home_x, by = d.stick ? d.by : d.home_y, kx = d.stick ? d.kx : bx, ky = d.stick ? d.ky : by, sr = d.stick_r;
    UIBezierPath *ring = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(bx - sr, by - sr, 2 * sr, 2 * sr)];
    [fill setFill]; [ring fill]; [line setStroke]; ring.lineWidth = 1.5; [ring stroke];
    CGFloat kr = sr * 0.42; UIBezierPath *knob = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(kx - kr, ky - kr, 2 * kr, 2 * kr)];
    [(d.stick ? down : fill) setFill]; [knob fill]; [knob stroke];
    if (d.editing) { UIBezierPath *o = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(bx - sr - 3, by - sr - 3, 2 * sr + 6, 2 * sr + 6)]; CGFloat dash[2] = {4, 3};
        [o setLineDash:dash count:2 phase:0]; o.lineWidth = d.sel == TP_SEL_STICK ? 2.5 : 1; [(d.sel == TP_SEL_STICK ? UIColor.systemYellowColor : [UIColor colorWithWhite:1 alpha:0.8]) setStroke]; [o stroke]; }
    if (d.look) { CGFloat lr = 22 * d.u; UIBezierPath *l = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(d.look_x - lr, d.look_y - lr, 2 * lr, 2 * lr)]; [line setStroke]; [l stroke]; }
}
- (void)startEdit {
    if (_bar) return;
    tp_edit(1); [self setNeedsDisplay];
    _bar = [[DmEditBar alloc] initWithPad:self];
    [self.superview addSubview:_bar];
}
- (void)endEdit:(BOOL)save {
    tp_edit(0);
    if (save) { char spec[1024]; tp_custom_get(spec, sizeof spec); g_layout_spec = @(spec); [NSUserDefaults.standardUserDefaults setObject:g_layout_spec forKey:@"dm.touch_layout"];
        LOG("front: controls layout saved: %s", spec[0] ? spec : "(the default)"); }
    [_bar removeFromSuperview]; _bar = nil; [self setNeedsDisplay];
}
@end
static __weak DmPadView *g_pad;

// The layout editor's bar (plan item 148), at the top of the screen while the editor is on: the size of the control touched last, Reset, Done
@implementation DmEditBar { __weak DmPadView *_pad; UISlider *_size; UILabel *_hint; }
- (instancetype)initWithPad:(DmPadView *)pad {
    CGRect b = pad.bounds; UIEdgeInsets in = pad.safeAreaInsets;
    if (!(self = [super initWithFrame:CGRectMake(b.size.width / 2 - 220, in.top + 8, 440, 44)])) return nil;
    _pad = pad; self.backgroundColor = [UIColor colorWithWhite:0.1 alpha:0.85]; self.layer.cornerRadius = 10;
    self.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin;
    _hint = [[UILabel alloc] initWithFrame:CGRectMake(12, 0, 120, 44)]; _hint.textColor = UIColor.whiteColor; _hint.font = [UIFont systemFontOfSize:13]; _hint.text = @"Drag a control";
    _size = [[UISlider alloc] initWithFrame:CGRectMake(132, 7, 150, 30)]; _size.minimumValue = 0.5; _size.maximumValue = 2; _size.enabled = NO;
    [_size addTarget:self action:@selector(sized) forControlEvents:UIControlEventValueChanged];
    UIButton *reset = [UIButton systemButtonWithPrimaryAction:[UIAction actionWithTitle:@"Reset" image:nil identifier:nil handler:^(UIAction *a) { (void)a; tp_custom_reset(); [self selectionChanged]; [pad setNeedsDisplay]; }]];
    UIButton *done = [UIButton systemButtonWithPrimaryAction:[UIAction actionWithTitle:@"Done" image:nil identifier:nil handler:^(UIAction *a) { (void)a; [pad endEdit:YES]; }]];
    reset.frame = CGRectMake(290, 7, 64, 30); done.frame = CGRectMake(362, 7, 64, 30);
    for (UIView *v in @[_hint, _size, reset, done]) [self addSubview:v];
    return self;
}
- (void)sized { tp_edit_scale(_size.value); [_pad setNeedsDisplay]; }
- (void)selectionChanged { float s = tp_edit_scale_get(); _size.enabled = s > 0; if (s > 0) { _size.value = s; _hint.text = @"Size"; } }
@end

// The settings panel (plan item 148; BotWRecomp's in-game settings and lordran-native's floating menu, MIT, the ideas): the Settings page's choices
// while the game runs, from the menu button between - and + or a three-finger tap (any game). It writes the same user defaults, so what applies
// while the game runs (frame rate, MetalFX, the frame rate line, the camera, the controls' opacity) applies at once; the render resolution at the
// next launch. The game goes on behind it.
@interface DmPanel : UIViewController
@end
@implementation DmPanel
static UIStackView *panel_row(NSString *title, UIView *ctl) {
    UILabel *l = [UILabel new]; l.text = title; l.textColor = UIColor.whiteColor; l.font = [UIFont systemFontOfSize:15];
    UIStackView *r = [[UIStackView alloc] initWithArrangedSubviews:@[l, ctl]]; r.axis = UILayoutConstraintAxisHorizontal; r.spacing = 12; r.alignment = UIStackViewAlignmentCenter;
    [l setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    return r;
}
static UISwitch *panel_switch(NSString *key) {
    UISwitch *s = [UISwitch new]; s.on = [NSUserDefaults.standardUserDefaults boolForKey:key];
    [s addAction:[UIAction actionWithHandler:^(UIAction *a) { [NSUserDefaults.standardUserDefaults setBool:((UISwitch *)a.sender).on forKey:key]; }] forControlEvents:UIControlEventValueChanged];
    return s;
}
static UISwitch *panel_switch_on(NSString *key) {   // a switch whose unset key reads as on (dm.hot_hold)
    UISwitch *s = panel_switch(key); id v = [NSUserDefaults.standardUserDefaults objectForKey:key]; s.on = v ? [v boolValue] : YES; return s;
}
static UISegmentedControl *panel_choice(NSString *key, NSArray<NSString *> *titles, NSArray<NSNumber *> *values) {
    UISegmentedControl *c = [[UISegmentedControl alloc] initWithItems:titles]; NSInteger v = [NSUserDefaults.standardUserDefaults integerForKey:key];
    c.selectedSegmentIndex = MAX(0, (NSInteger)[values indexOfObject:@(v)] == NSNotFound ? 0 : (NSInteger)[values indexOfObject:@(v)]);
    [c addAction:[UIAction actionWithHandler:^(UIAction *a) { [NSUserDefaults.standardUserDefaults setInteger:values[((UISegmentedControl *)a.sender).selectedSegmentIndex].integerValue forKey:key]; }]
        forControlEvents:UIControlEventValueChanged];
    return c;
}
static UISlider *panel_slider(NSString *key, float lo, float hi, float def, NSArray<NSNumber *> *steps) {   // steps: the values the Settings page lists
    UISlider *s = [UISlider new]; s.minimumValue = lo; s.maximumValue = hi; float v = [NSUserDefaults.standardUserDefaults floatForKey:key]; s.value = v > 0 ? v : def;
    [s.widthAnchor constraintEqualToConstant:160].active = YES;
    [s addAction:[UIAction actionWithHandler:^(UIAction *a) { UISlider *x = (UISlider *)a.sender; float best = x.value, bd = 1e9f;
        for (NSNumber *n in steps) if (fabsf(n.floatValue - x.value) < bd) { bd = fabsf(n.floatValue - x.value); best = n.floatValue; }
        [NSUserDefaults.standardUserDefaults setFloat:best forKey:key]; }] forControlEvents:UIControlEventValueChanged];
    return s;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    UIView *card = [UIView new]; card.backgroundColor = [UIColor colorWithWhite:0.12 alpha:0.95]; card.layer.cornerRadius = 14; card.translatesAutoresizingMaskIntoConstraints = NO;
    UIScrollView *sc = [UIScrollView new]; sc.translatesAutoresizingMaskIntoConstraints = NO;
    UIStackView *st = [UIStackView new]; st.axis = UILayoutConstraintAxisVertical; st.spacing = 10; st.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:card]; [card addSubview:sc]; [sc addSubview:st];
    UILabel *t = [UILabel new]; t.text = @"Settings"; t.textColor = UIColor.whiteColor; t.font = [UIFont boldSystemFontOfSize:18]; [st addArrangedSubview:t];
    [st addArrangedSubview:panel_row(@"Frame rate", panel_choice(@"dm.fps", @[@"60", @"30"], @[@60, @30]))];
    [st addArrangedSubview:panel_row(@"Hold 30 fps when hot", panel_switch_on(@"dm.hot_hold"))];
    [st addArrangedSubview:panel_row(@"Render resolution (next launch)", panel_choice(@"dm.render_height", @[@"Game", @"Native", @"900", @"720", @"540"], @[@0, @-1, @900, @720, @540]))];
    [st addArrangedSubview:panel_row(@"MetalFX upscaling", panel_switch(@"dm.metalfx"))];
    [st addArrangedSubview:panel_row(@"Show frame rate", panel_switch(@"dm.perf_line"))];
    if (g_pad) {
        [st addArrangedSubview:panel_row(@"Camera sensitivity", panel_slider(@"dm.look_sensitivity", 0.25f, 3, 1, @[@0.25, @0.5, @0.75, @1, @1.25, @1.5, @2, @2.5, @3]))];
        [st addArrangedSubview:panel_row(@"Invert horizontal", panel_switch(@"dm.look_invert_x"))];
        [st addArrangedSubview:panel_row(@"Invert vertical", panel_switch(@"dm.look_invert_y"))];
        [st addArrangedSubview:panel_row(@"Controls opacity", panel_slider(@"dm.touch_opacity", 0.15f, 1, 1, @[@0.3, @0.5, @0.75, @1]))];
        __weak DmPanel *ws = self;
        UIButton *edit = [UIButton systemButtonWithPrimaryAction:[UIAction actionWithTitle:@"Edit controls layout" image:nil identifier:nil handler:^(UIAction *a) { (void)a;
            [ws dismissViewControllerAnimated:YES completion:^{ [g_pad startEdit]; }]; }]];
        UIButton *reset = [UIButton systemButtonWithPrimaryAction:[UIAction actionWithTitle:@"Reset controls layout" image:nil identifier:nil handler:^(UIAction *a) { (void)a;
            tp_custom_reset(); g_layout_spec = @""; [NSUserDefaults.standardUserDefaults setObject:@"" forKey:@"dm.touch_layout"]; [g_pad setNeedsDisplay]; }]];
        [st addArrangedSubview:edit]; [st addArrangedSubview:reset];
    }
    __weak DmPanel *ws = self;
    UIButton *done = [UIButton systemButtonWithPrimaryAction:[UIAction actionWithTitle:@"Done" image:nil identifier:nil handler:^(UIAction *a) { (void)a; [ws dismissViewControllerAnimated:YES completion:nil]; }]];
    [st addArrangedSubview:done];
    UILayoutGuide *g = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [card.centerXAnchor constraintEqualToAnchor:g.centerXAnchor], [card.centerYAnchor constraintEqualToAnchor:g.centerYAnchor],
        [card.widthAnchor constraintEqualToConstant:460], [card.heightAnchor constraintLessThanOrEqualToAnchor:g.heightAnchor constant:-16],
        [sc.topAnchor constraintEqualToAnchor:card.topAnchor constant:14], [sc.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-14],
        [sc.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:18], [sc.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-18],
        [st.topAnchor constraintEqualToAnchor:sc.contentLayoutGuide.topAnchor], [st.bottomAnchor constraintEqualToAnchor:sc.contentLayoutGuide.bottomAnchor],
        [st.leadingAnchor constraintEqualToAnchor:sc.contentLayoutGuide.leadingAnchor], [st.trailingAnchor constraintEqualToAnchor:sc.contentLayoutGuide.trailingAnchor],
        [st.widthAnchor constraintEqualToAnchor:sc.frameLayoutGuide.widthAnchor],
    ]];
    NSLayoutConstraint *h = [card.heightAnchor constraintEqualToAnchor:st.heightAnchor constant:28]; h.priority = UILayoutPriorityDefaultHigh; h.active = YES;
}
@end
static void front_panel_open(void) {   // main thread
    UIViewController *top = top_controller();
    if (!top || [top isKindOfClass:DmPanel.class]) return;
    DmPanel *p = [DmPanel new]; p.modalPresentationStyle = UIModalPresentationOverFullScreen; p.modalTransitionStyle = UIModalTransitionStyleCrossDissolve;
    LOG("front: settings panel");
    [top presentViewController:p animated:YES completion:nil];
}
@interface DmPanelTap : NSObject
@end
@implementation DmPanelTap
- (void)tap:(UITapGestureRecognizer *)r { if (r.state == UIGestureRecognizerStateEnded) front_panel_open(); }
@end
static DmPanelTap *g_panel_tap;
static int pad_wanted(void) {
    const char *e = getenv("DM_TOUCH");
    if (e && !strcmp(e, "pad")) return 1;
    if (e && !strcmp(e, "screen")) return 0;
    return dm_game.touch_pad;
}
// A game controller (plan item 171, after Touchable): connecting one leaves the controls; a key on it hides them (dm_front_controller_key, host/
// gamepad.m); a touch anywhere (DmAnyTouch on the screen's view) or the last controller gone shows them again (tp_controller)
static void pad_ctl_apply(int hidden) { if (!g_pad) return; g_pad.hidden = hidden; [g_pad setNeedsDisplay]; }
static void pad_follow_controllers(void) {   // main thread
    int ctl = 0; for (GCController *c in GCController.controllers) ctl |= c.extendedGamepad != nil;
    pad_ctl_apply(tp_controller(ctl ? TP_CTL_CONNECT : TP_CTL_GONE));
}
void dm_front_controller_key(void) { dispatch_async(dispatch_get_main_queue(), ^{ if (g_pad && !g_pad.hidden) pad_ctl_apply(tp_controller(TP_CTL_KEY)); }); }
@interface DmAnyTouch : UIGestureRecognizer
@end
@implementation DmAnyTouch
- (void)touchesBegan:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e {
    (void)t; (void)e;
    if (g_pad && g_pad.hidden) pad_ctl_apply(tp_controller(TP_CTL_TOUCH));
    self.state = UIGestureRecognizerStateFailed;   // only watches: the touch goes on to the views
}
@end

// Frame rate hint for ProMotion screens: 60 Hz, or 30 Hz while the graphics layer is in its power-saving mode
// System volume changes are logged with the time since the display link's last callback: after a volume button press the display link was seen to
// stop while active (nvn_mtl.m rescue), and the log otherwise does not show when the button was pressed
double mtl_link_tick_age_ms(void);
@interface DmVolumeWatch : NSObject
@end
@implementation DmVolumeWatch
- (void)observeValueForKeyPath:(NSString *)k ofObject:(id)o change:(NSDictionary *)c context:(void *)x {
    (void)k; (void)o; (void)x;
    LOG("front: system volume %.2f → %.2f (display link last called back %.0f ms ago)", [c[NSKeyValueChangeOldKey] doubleValue], [c[NSKeyValueChangeNewKey] doubleValue], mtl_link_tick_age_ms());
}
@end
@interface DmRateHint : NSObject
- (void)tick:(CADisplayLink *)l;
@end
// A game stepping by the measured time asks a ProMotion screen for 80-120 Hz and tells the graphics layer the rate it runs at (plan item 172,
// nvn_display_rate_now); 60 or 30 otherwise
void nvn_display_rate_now(int now_hz);
int dm_game_realtime_step(void);
@implementation DmRateHint
- (void)tick:(CADisplayLink *)l {
    static int last = -1; int f = mtl_fps_target();
    int pro = f != 30 && g_view.window.windowScene.screen.maximumFramesPerSecond >= 120 && dm_game_realtime_step() && !getenv("DM_PRESENT_HZ") && !(getenv("DM_PROMOTION") && !strcmp(getenv("DM_PROMOTION"), "0"));
    int want = pro ? 120 : f;
    if (want != last) { last = want; l.preferredFrameRateRange = want == 120 ? CAFrameRateRangeMake(80, 120, 120) : want == 30 ? CAFrameRateRangeMake(30, 30, 30) : CAFrameRateRangeMake(60, 60, 60); }
    if (pro) { double d = l.targetTimestamp - l.timestamp; if (d > 0) nvn_display_rate_now((int)lround(1.0 / d)); }
}
@end
static CADisplayLink *g_rate_link;

UIView *front_ios_view(CGRect frame, UIScreen *screen) {
    CGFloat scale = screen.nativeScale; screen_note(screen);
    DmLetterboxView *box = [[DmLetterboxView alloc] initWithFrame:frame];
    box.backgroundColor = UIColor.blackColor;
    if (@available(iOS 27.1, *))   // the hinge's state (the iPhone Duo; a device without one never calls back with a hinge): logged when it changes
        [box addInteraction:[[UIHingeInteraction alloc] initWithUpdateHandler:^(UIHingeInteraction *i, UIHingeInteractionUpdate *u) {
            (void)i; static long last = -2; UIHinge *h = u.hinge; long st = h ? (long)h.status : -1;
            static const char *name[] = {"unknown", "closed", "partially open", "fully open"};
            if (st == last || st < -1 || st > 3) return;
            last = st;
            if (h) LOG("front: hinge %s, %.0f degrees", name[st], h.angle * 180 / M_PI); else LOG("front: no hinge updates (the view left a hierarchy that has them)"); }]];
    DmMetalView *v = [[DmMetalView alloc] initWithFrame:letterbox_rect(frame)];
    box.game = v; [box addSubview:v]; g_view = v;
    if (pad_wanted()) {
        DmPadView *pad = [[DmPadView alloc] initWithFrame:box.bounds];
        pad.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [box addSubview:pad]; g_pad = pad;
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; NSOperationQueue *mq = NSOperationQueue.mainQueue;
        for (NSNotificationName n in @[GCControllerDidConnectNotification, GCControllerDidDisconnectNotification])
            [nc addObserverForName:n object:nil queue:mq usingBlock:^(NSNotification *x) { (void)x; pad_follow_controllers(); }];
        pad_follow_controllers();
        DmAnyTouch *any = [[DmAnyTouch alloc] initWithTarget:nil action:nil]; any.cancelsTouchesInView = NO; any.delaysTouchesBegan = NO; any.delaysTouchesEnded = NO;
        [box addGestureRecognizer:any];
        LOG("front: on-screen controls (touch \"pad\"%s)", getenv("DM_TOUCH") ? ", from DM_TOUCH" : "");
    }
    g_panel_tap = [DmPanelTap new];   // a three-finger tap opens the settings panel in any game (plan item 148); the touches still reach the game
    UITapGestureRecognizer *tap3 = [[UITapGestureRecognizer alloc] initWithTarget:g_panel_tap action:@selector(tap:)]; tap3.numberOfTouchesRequired = 3; tap3.cancelsTouchesInView = NO;
    [box addGestureRecognizer:tap3];
    g_layer = (CAMetalLayer *)v.layer;
    g_layer.device = MTLCreateSystemDefaultDevice();
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.contentsScale = scale;
    g_layer.drawableSize = drawable_size(v.bounds.size, g_layer.contentsScale);
    g_rate_link = [CADisplayLink displayLinkWithTarget:[DmRateHint new] selector:@selector(tick:)];
    g_rate_link.preferredFrameRateRange = CAFrameRateRangeMake(60, 60, 60);
    [g_rate_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    double gw, gh; game_size(&gw, &gh);
    LOG("front: window %.0fx%.0f, game picture %.0fx%.0f, game view %.0fx%.0f at (%.0f, %.0f)", frame.size.width, frame.size.height, gw, gh,
        v.frame.size.width, v.frame.size.height, v.frame.origin.x, v.frame.origin.y);
    dm_gamepad_start();
    return box;
}
void *front_metal_layer(void) { return (__bridge void *)g_layer; }

void front_run(void (*guest)(void)) {
    static char alt[65536];   // the alternate signal stack is per thread: the crash report must run even when a guest thread overflows its stack
    stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt}; sigaltstack(&ss, 0);
    // This thread inherits the UIKit main thread's signal mask, which blocks SIGUSR2 / SIGALRM (measured on a device); the host's diagnostics need them
    sigset_t all; sigfillset(&all); pthread_sigmask(SIG_UNBLOCK, &all, NULL);
    guest();
}

// ---------- Presenting on the main thread ----------
static UIViewController *top_controller(void) {   // key window first; else a window of the active scene; else any window
    UIViewController *vc = nil, *alt = nil, *any = nil;
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class]) for (UIWindow *w in ((UIWindowScene *)s).windows) {
            if (!w.rootViewController) continue;
            if (w.isKeyWindow) vc = w.rootViewController;
            else if (s.activationState == UISceneActivationStateForegroundActive && !alt) alt = w.rootViewController;
            else if (!any) any = w.rootViewController;
        }
    if (!vc) vc = alt ?: any;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

// Software keyboard (front.h): the main thread shows an alert with a text field, the calling guest thread waits for OK / Cancel. max_chars limits the
// text while typing (UTF-16 units, as swkbd counts; text still being composed by an input method is left alone).
// Test switch DM_TEST_KBD_TYPE="text" or ":cancel": 1.5 s after the dialog appears the text is filled in through the same editing path, then OK / Cancel
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    if (!g_layer) return -1;
    NSString *t = title && *title ? @(title) : @"Enter text";
    NSString *msg = [NSString stringWithFormat:@"%@%@%@", sub ? @(sub) : @"", sub && *sub && guide && *guide ? @"\n" : @"", guide ? @(guide) : @""];
    NSString *init = initial ? @(initial) : @"", *ph = guide && *guide ? @(guide) : nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block int r = 0; __block NSString *text = nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *a = [UIAlertController alertControllerWithTitle:t message:msg.length ? msg : nil preferredStyle:UIAlertControllerStyleAlert];
        a.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
        __weak UIAlertController *wa = a;
        [a addTextFieldWithConfigurationHandler:^(UITextField *f) {
            f.text = init; f.placeholder = ph; f.clearButtonMode = UITextFieldViewModeWhileEditing; f.keyboardAppearance = UIKeyboardAppearanceDark;
            if (max_chars > 0) [f addAction:[UIAction actionWithHandler:^(UIAction *act) {
                UITextField *tf = (UITextField *)act.sender;
                if (tf.markedTextRange || (int)tf.text.length <= max_chars) return;
                NSRange keep = [tf.text rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, (NSUInteger)max_chars)];
                if ((int)NSMaxRange(keep) > max_chars) keep = [tf.text rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, (NSUInteger)max_chars - 1)];
                tf.text = [tf.text substringWithRange:keep];
            }] forControlEvents:UIControlEventEditingChanged];
        }];
        void (^finish)(int) = ^(int ok) { if (ok) text = wa.textFields.firstObject.text; r = ok; dispatch_semaphore_signal(done); };
        [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x) { (void)x; finish(0); }]];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { (void)x; finish(1); }]];
        UIViewController *vc = top_controller();
        if (!vc) { LOG("[red] front: no view controller to show the keyboard dialog on; cancelled"); finish(0); return; }
        [vc presentViewController:a animated:YES completion:^{
            const char *tk = getenv("DM_TEST_KBD_TYPE"); if (!tk) return;
            NSString *typed = @(tk);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500000000), dispatch_get_main_queue(), ^{
                int cancel = [typed isEqualToString:@":cancel"];
                UITextField *tf = a.textFields.firstObject;
                if (!cancel) { tf.text = typed; [tf sendActionsForControlEvents:UIControlEventEditingChanged]; }
                LOG("front: DM_TEST_KBD_TYPE %s, text field now \"%s\"", cancel ? "cancel" : "typed", tf.text.UTF8String ?: "");
                [a dismissViewControllerAnimated:NO completion:^{ finish(!cancel); }];
            });
        }];
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (!r) return 0;
    snprintf(out, (size_t)out_cap, "%s", text.UTF8String ? text.UTF8String : "");
    return 1;
}

// A save transaction was refused (hle_fs.c, on the game thread): tell the player without blocking; the next transaction commits as usual
void front_save_failed(int err) {
    LOG("front: save write failed, errno=%d", err);
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *vc = top_controller();
        if (!vc) { LOG("[red] front: no view controller for the save failure alert"); return; }
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Not enough storage" message:@"The game could not save its progress." preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [vc presentViewController:a animated:YES completion:nil];
    });
}

// ---------- Lifecycle → host (hle.h: dm_focus_set, dm_audio_suspend; dm.h: dm_gfx_suspend, guest_clock_freeze_for) ----------
// Audio pauses for two independent reasons, background and an audio interruption (call, alarm); it resumes only when both are gone
static int g_audio_bg, g_audio_intr;
static void audio_apply(void) { dm_audio_suspend(g_audio_bg || g_audio_intr); }
// Scene events from main.m: 0 resign active, 1 enter background, 2 return to the foreground, 3 become active.
// In the background the guest clock is frozen too (like a Switch suspend), so in-game time does not jump by the time spent away
void front_ios_focus_event(int ev) {
    static const char *k[] = {"sceneWillResignActive", "sceneDidEnterBackground", "sceneWillEnterForeground", "sceneDidBecomeActive"};
    LOG("front: %s (guest clock %.3f s, audio callbacks %llu)", k[ev], guest_ns() / 1e9, (unsigned long long)dm_audio_render_calls());
    switch (ev) {
    case 0: [g_view liftAll]; if (g_pad) pad_follow_controllers(); dm_gamepad_reset(); dm_focus_set(2); break;   // held buttons / touches released in the background never report it
    case 1: dm_focus_set(3); g_audio_bg = 1; audio_apply(); dm_cloud_flush(1); guest_clock_freeze_for(1, 1); dm_gfx_suspend(1); break;
    // Back in the foreground any interruption counts as over: the system interrupts the session when the app goes to the background but does not
    // promise an "ended" on return; if the session is really still taken, starting the audio unit fails harmlessly and is retried on activation
    case 2: dm_cloud_foreground(); dm_gfx_suspend(0); guest_clock_freeze_for(1, 0); g_audio_bg = g_audio_intr = 0; audio_apply(); dm_focus_set(2); break;
    case 3: dm_focus_set(1); audio_apply(); break;
    }
}

// Called by main.m when the host starts: no auto-lock, thermal logging, audio session (Playback: sound plays with the mute switch on, as games do,
// mixed with other apps' sound; stopping in the background and on interruptions is handled here)
void front_ios_lifecycle_install(void) {
    UIApplication.sharedApplication.idleTimerDisabled = YES;
    NSProcessInfo *pi = NSProcessInfo.processInfo;
    int sustained = -1; if (@available(iOS 18.0, *)) sustained = (int)[pi hasPerformanceProfile:NSProcessPerformanceProfileSustained];   // -1: no such query before iOS 18
    LOG("front: thermal state %ld, sustained performance profile %d", (long)pi.thermalState, sustained);
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; NSOperationQueue *mq = NSOperationQueue.mainQueue;
    [nc addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil queue:mq usingBlock:^(NSNotification *n) {
        (void)n; LOG("front: thermal state %ld", (long)NSProcessInfo.processInfo.thermalState); }];
    AVAudioSession *as = AVAudioSession.sharedInstance; NSError *e = nil;
    // Playback: sound with the silent switch on. MixWithOthers: Playback alone does not mix, and activating the session interrupted other apps'
    // sound (music, a voice call, screen sharing) for as long as the game was in front, whatever its volume (upstream 9393b00, #77)
    // 48 kHz (the game's rate: no resampling) and a 5 ms I/O buffer (plan item 134): asked for nothing, RemoteIO pulled about 1,024 frames a callback
    // against the 4 x 240 frames queued, so callbacks underran (nxnative 18c6143; Touchable asks the same). The actual values are logged: the system
    // may give others (a Bluetooth route)
    static void (^setup)(void) = ^{ AVAudioSession *s = AVAudioSession.sharedInstance; NSError *er = nil;
        if (![s setCategory:AVAudioSessionCategoryPlayback withOptions:AVAudioSessionCategoryOptionMixWithOthers error:&er]) LOG("[red] front: audio session category: %s", er.localizedDescription.UTF8String);
        if (![s setPreferredSampleRate:48000 error:&er]) LOG("[red] front: audio session sample rate: %s", er.localizedDescription.UTF8String);
        if (![s setPreferredIOBufferDuration:0.005 error:&er]) LOG("[red] front: audio session I/O buffer: %s", er.localizedDescription.UTF8String);
        if (![s setActive:YES error:&er]) LOG("[red] front: audio session activation: %s", er.localizedDescription.UTF8String);
        LOG("front: audio session %.0f Hz, I/O buffer %.2f ms (asked 48000 Hz, 5 ms), output latency %.1f ms", s.sampleRate, s.IOBufferDuration * 1000, s.outputLatency * 1000); };
    setup(); (void)e;
    LOG("front: audio session category %s, options %#lx (mixes with other apps: %s), other audio playing %d", as.category.UTF8String, (unsigned long)as.categoryOptions,
        (as.categoryOptions & AVAudioSessionCategoryOptionMixWithOthers) ? "yes" : "no", (int)as.isOtherAudioPlaying);
    [nc addObserverForName:AVAudioSessionInterruptionNotification object:as queue:mq usingBlock:^(NSNotification *n) {
        AVAudioSessionInterruptionType t = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        if (t == AVAudioSessionInterruptionTypeBegan) { LOG("front: audio interrupted"); g_audio_intr = 1; audio_apply(); return; }
        // Ended: clear the reason whether or not the system says "should resume" (a dismissed alarm comes without it)
        g_audio_intr = 0; [AVAudioSession.sharedInstance setActive:YES error:nil]; audio_apply();
        LOG("front: audio interruption ended%s", g_audio_bg ? " (still in the background)" : "");
    }];
    // The media services were reset (the audio server restarted): every AudioUnit is invalid; set the session up again and make the units again
    [nc addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:as queue:mq usingBlock:^(NSNotification *n) {
        (void)n; LOG("front: audio media services were reset"); setup(); dm_audio_reset(); }];
    static DmVolumeWatch *vw; vw = [DmVolumeWatch new];
    [as addObserver:vw forKeyPath:@"outputVolume" options:NSKeyValueObservingOptionOld | NSKeyValueObservingOptionNew context:NULL];
    [nc addObserverForName:AVAudioSessionRouteChangeNotification object:as queue:mq usingBlock:^(NSNotification *n) {
        LOG("front: audio route changed, reason %lu (display link last called back %.0f ms ago)", (unsigned long)[n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue], mtl_link_tick_age_ms()); }];
    // Test switch DM_TEST_BG=<start s>,<duration s>[;...]: simulated "enter background → return" through the same handlers
    const char *tb = getenv("DM_TEST_BG"); double tb_at, tb_dur;
    for (const char *tp = tb; tp && sscanf(tp, "%lf,%lf", &tb_at, &tb_dur) == 2; tp = strchr(tp, ';') ? strchr(tp, ';') + 1 : NULL) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(tb_at * 1e9)), dispatch_get_main_queue(), ^{
            LOG("front: DM_TEST_BG enter background"); front_ios_focus_event(0); front_ios_focus_event(1); });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((tb_at + tb_dur) * 1e9)), dispatch_get_main_queue(), ^{
            LOG("front: DM_TEST_BG return"); front_ios_focus_event(2); front_ios_focus_event(3); });
    }
}
