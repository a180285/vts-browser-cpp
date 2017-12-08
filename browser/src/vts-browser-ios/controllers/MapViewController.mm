/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <map>
#include <set>
#include "../Map.h"
#include <vts-browser/draws.hpp>
#include <vts-browser/math.hpp>
#include <vts-browser/options.hpp>
#include <vts-browser/exceptions.hpp>
#include <vts-renderer/renderer.hpp>

#import "MapViewController.h"

using namespace vts;


@interface MapViewController ()
{
    std::map<UIGestureRecognizer*, std::set<UIGestureRecognizer*>> gesturesRequireFail;

    UIPanGestureRecognizer *rPanSingle, *rYawSingle, *rPitchSingle, *rZoomSingle;
    UIPanGestureRecognizer *rPanMulti, *rPitchMultiEval;
    UISwipeGestureRecognizer *rPitchMultiInitUp, *rPitchMultiInitDown;
    UIRotationGestureRecognizer *rYawMulti;
    UIPinchGestureRecognizer *rZoomMulti;
    UILongPressGestureRecognizer *rNorth;
    UITapGestureRecognizer *rFullscreen, *rGoto;
    UIPanGestureRecognizer *rCompas;

    vec2 gotoPoint;
    bool fullscreenStatus;
    bool fullscreenOverride;
    bool pitchMultiEnabled;
}

@property (weak, nonatomic) IBOutlet UIView *gestureViewCenter;
@property (weak, nonatomic) IBOutlet UIView *gestureViewBottom;
@property (weak, nonatomic) IBOutlet UIView *gestureViewLeft;
@property (weak, nonatomic) IBOutlet UIView *gestureViewRight;
@property (weak, nonatomic) IBOutlet UIView *gestureViewCompas;
@property (weak, nonatomic) IBOutlet UIActivityIndicatorView *activityIndicator;

@property (strong, nonatomic) UIBarButtonItem *searchButton;

@end


@implementation MapViewController

// gestures

- (void)gesturePan:(UIPanGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            if (extraConfig.controlType == 0 || !pitchMultiEnabled)
            {
                CGPoint p = [recognizer translationInView:self.view];
                map->pan({p.x * 1.5, p.y * 1.5, 0});
                map->options().navigationType = vts::NavigationType::Quick;
            }
            [recognizer setTranslation:CGPoint() inView:self.view];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureYawSingle:(UIPanGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            CGPoint p = [recognizer translationInView:self.view];
            map->rotate({2000 * p.x / renderOptions.width, 0, 0});
            map->options().navigationType = vts::NavigationType::Quick;
            [recognizer setTranslation:CGPoint() inView:self.view];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gesturePitchSingle:(UIPanGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            CGPoint p = [recognizer translationInView:self.view];
            map->rotate({0, 2000 * p.y / renderOptions.height, 0});
            map->options().navigationType = vts::NavigationType::Quick;
            [recognizer setTranslation:CGPoint() inView:self.view];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureZoomSingle:(UIPanGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            CGPoint p = [recognizer translationInView:self.view];
            map->zoom(-60 * p.y / renderOptions.height);
            map->options().navigationType = vts::NavigationType::Quick;
            [recognizer setTranslation:CGPoint() inView:self.view];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureYawMulti:(UIRotationGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            if (!pitchMultiEnabled)
            {
                map->rotate({-380 * recognizer.rotation, 0, 0});
                map->options().navigationType = vts::NavigationType::Quick;
            }
            [recognizer setRotation:0];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gesturePitchMultiInit:(UISwipeGestureRecognizer*)recognizer
{
    pitchMultiEnabled = true;
}

- (void)gesturePitchMultiEval:(UIPanGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateFailed:
        case UIGestureRecognizerStateCancelled:
        {
            pitchMultiEnabled = false;
        } break;
        case UIGestureRecognizerStateChanged:
        {
            if (pitchMultiEnabled)
            {
                CGPoint p = [recognizer translationInView:self.view];
                map->rotate({0, 7000 * p.y / renderOptions.height, 0});
                map->options().navigationType = vts::NavigationType::Quick;
            }
            [recognizer setTranslation:CGPoint() inView:self.view];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureZoomMulti:(UIPinchGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            if (!pitchMultiEnabled)
            {
                map->zoom(6 * (recognizer.scale - 1));
                map->options().navigationType = vts::NavigationType::Quick;
            }
            [recognizer setScale:1];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureNorthUp:(UILongPressGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateBegan:
        {
            map->setPositionRotation({0,270,0});
            map->options().navigationType = vts::NavigationType::Quick;
            map->resetNavigationMode();
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureGoto:(UITapGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        {
            CGPoint point = [recognizer locationInView:self.view];
            double x = point.x * self.view.contentScaleFactor;
            double y = point.y * self.view.contentScaleFactor;
            gotoPoint = vec2(x, y);
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (void)gestureFullscreen:(UITapGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        {
            fullscreenOverride = !fullscreenOverride;
        } break;
        default:
            break;
    }
}

- (void)gestureCompas:(UIPanGestureRecognizer*)recognizer
{
    switch (recognizer.state)
    {
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateChanged:
        {
            {
                double speed = 2500.0 / self.view.frame.size.width;
                CGPoint p = [recognizer translationInView:self.view];
                map->rotate({p.x * speed, p.y * speed, 0});
                map->options().navigationType = vts::NavigationType::Quick;
            }
            [recognizer setTranslation:CGPoint() inView:self.view];
            fullscreenOverride = false;
        } break;
        default:
            break;
    }
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)otherGestureRecognizer
{
    if (gestureRecognizer == rCompas || otherGestureRecognizer == rCompas)
        return false;
    return extraConfig.controlType == 1;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer shouldRequireFailureOfGestureRecognizer:(UIGestureRecognizer*)otherGestureRecognizer
{
    auto s = gesturesRequireFail[gestureRecognizer];
    return s.find(otherGestureRecognizer) != s.end();
}

- (void)initializeGestures
{
    assert(_gestureViewCenter);
    assert(_gestureViewBottom);
    assert(_gestureViewLeft);
    assert(_gestureViewRight);
    assert(_gestureViewCompas);

    gesturesRequireFail.clear();

    // single touch
    {
        // pan recognizer
        UIPanGestureRecognizer *r = rPanSingle = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gesturePan:)];
        r.maximumNumberOfTouches = 1;
        [_gestureViewCenter addGestureRecognizer:r];
    }
    {
        // yaw recognizer
        UIPanGestureRecognizer *r = rYawSingle = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gestureYawSingle:)];
        r.maximumNumberOfTouches = 1;
        [_gestureViewBottom addGestureRecognizer:r];
    }
    {
        // pitch recognizer
        UIPanGestureRecognizer *r = rPitchSingle = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gesturePitchSingle:)];
        r.maximumNumberOfTouches = 1;
        [_gestureViewLeft addGestureRecognizer:r];
    }
    {
        // zoom recognizer
        UIPanGestureRecognizer *r = rZoomSingle = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gestureZoomSingle:)];
        r.maximumNumberOfTouches = 1;
        [_gestureViewRight addGestureRecognizer:r];
    }

    // multitouch
    {
        // pan recognizer
        UIPanGestureRecognizer *r = rPanMulti = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gesturePan:)];
        r.maximumNumberOfTouches = 2;
        [self.view addGestureRecognizer:r];
    }
    {
        // yaw recognizer
        UIRotationGestureRecognizer *r = rYawMulti = [[UIRotationGestureRecognizer alloc] initWithTarget:self action:@selector(gestureYawMulti:)];
        [self.view addGestureRecognizer:r];
    }
    {
        // pitch recognizer init up
        UISwipeGestureRecognizer *r = rPitchMultiInitUp = [[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(gesturePitchMultiInit:)];
        r.numberOfTouchesRequired = 2;
        r.direction = UISwipeGestureRecognizerDirectionUp;
        [self.view addGestureRecognizer:r];
    }
    {
        // pitch recognizer init down
        UISwipeGestureRecognizer *r = rPitchMultiInitDown = [[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(gesturePitchMultiInit:)];
        r.numberOfTouchesRequired = 2;
        r.direction = UISwipeGestureRecognizerDirectionDown;
        [self.view addGestureRecognizer:r];
    }
    {
        // pitch recognizer eval
        UIPanGestureRecognizer *r = rPitchMultiEval = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gesturePitchMultiEval:)];
        r.minimumNumberOfTouches = r.maximumNumberOfTouches = 2;
        [self.view addGestureRecognizer:r];
    }
    {
        // zoom recognizer
        UIPinchGestureRecognizer *r = rZoomMulti = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(gestureZoomMulti:)];
        [self.view addGestureRecognizer:r];
    }
    gesturesRequireFail[rPanMulti].insert(rPitchMultiInitUp);
    gesturesRequireFail[rYawMulti].insert(rPitchMultiInitUp);
    gesturesRequireFail[rZoomMulti].insert(rPitchMultiInitUp);
    gesturesRequireFail[rPanMulti].insert(rPitchMultiInitDown);
    gesturesRequireFail[rYawMulti].insert(rPitchMultiInitDown);
    gesturesRequireFail[rZoomMulti].insert(rPitchMultiInitDown);

    // extra recognizers
    {
        // north up recognizer
        UILongPressGestureRecognizer *r = rNorth = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(gestureNorthUp:)];
        [self.view addGestureRecognizer:r];
    }
    {
        // goto
        UITapGestureRecognizer *r = rGoto = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(gestureGoto:)];
        r.numberOfTapsRequired = 2;
        [self.view addGestureRecognizer:r];
    }
    {
        // fullscreen
        UITapGestureRecognizer *r = rFullscreen = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(gestureFullscreen:)];
        [self.view addGestureRecognizer:r];
    }
    {
        // compas
        UIPanGestureRecognizer *r = rCompas = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(gestureCompas:)];
        r.maximumNumberOfTouches = 1;
        [_gestureViewCompas addGestureRecognizer:r];
    }
    gesturesRequireFail[rFullscreen].insert(rGoto);
    gesturesRequireFail[rFullscreen].insert(rNorth);

    // set delegate for all recognizers
    for (int i = 0, e = self.view.gestureRecognizers.count; i != e; i++)
        [[self.view.gestureRecognizers objectAtIndex:i] setDelegate:self];
}

- (void)updateGestures
{
    rPanSingle.enabled = rYawSingle.enabled = rPitchSingle.enabled = rZoomSingle.enabled = extraConfig.controlType == 0;
    rPanMulti.enabled = rYawMulti.enabled = rPitchMultiInitUp.enabled = rPitchMultiInitDown.enabled = rPitchMultiEval.enabled = rZoomMulti.enabled = extraConfig.controlType == 1;
    pitchMultiEnabled = false;
    rCompas.enabled = extraConfig.showControlCompas;
    _gestureViewCompas.userInteractionEnabled = extraConfig.showControlCompas;
}

// controller status

- (BOOL)prefersStatusBarHidden
{
    if (fullscreenStatus)
        return true;
    return [super prefersStatusBarHidden];
}

- (void)updateFullscreen
{
    bool enable = !fullscreenOverride;
    fullscreenStatus = enable;
    [self setNeedsStatusBarAppearanceUpdate];
    [self.navigationController setNavigationBarHidden:enable animated:YES];
}

- (void)configureView
{
    self.title = _item.name;
    {
        std::string url([_item.url UTF8String]);
        map->setMapConfigPath(url);
    }
}

- (void)setItem:(ConfigItem*)item
{
    _item = item;
}

- (void)showSearch
{
    [self performSegueWithIdentifier:@"search" sender:self];
}

- (void)showOptions
{
    [self performSegueWithIdentifier:@"position" sender:self];
}

// view controls

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    _gestureViewBottom.constraints.firstObject.constant = extraConfig.touchSize;
    _gestureViewLeft.constraints.firstObject.constant = extraConfig.touchSize;
    _gestureViewRight.constraints.firstObject.constant = extraConfig.touchSize;
    [self updateGestures];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    [self updateFullscreen];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    
    _searchButton = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSearch target:self action:@selector(showSearch)];
    self.navigationItem.rightBarButtonItems = @[
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemOrganize target:self action:@selector(showOptions)],
        _searchButton
    ];

    gotoPoint(0) = std::numeric_limits<double>::quiet_NaN();
    fullscreenStatus = false;
    fullscreenOverride = true;

    // initialize rendering
    GLKView *view = (GLKView *)self.view;
    view.context = mapRenderContext();

    [self initializeGestures];
    [self configureView];
}

// rendering

- (void)progressUpdate
{
    if (map->getMapConfigReady())
        [_activityIndicator stopAnimating];
    else
        [_activityIndicator startAnimating];
}

- (void)update
{
    mapTimerStop();

    _searchButton.enabled = map->searchable();

    try
    {
        map->renderTickPrepare();
        map->renderTickRender();
    }
    catch (const vts::MapConfigException &)
    {
        [[self navigationController] popViewControllerAnimated:YES];
        return;
    }

    [self updateFullscreen];
    [self progressUpdate];
}

- (void)glkView:(nonnull GLKView *)view drawInRect:(CGRect)rect
{
    {
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        renderOptions.targetFrameBuffer = fbo;
    }

    vts::renderer::render(renderOptions, map->draws(), map->celestialBody());
    glBindFramebuffer(GL_FRAMEBUFFER, renderOptions.targetFrameBuffer);

    if (gotoPoint(0) == gotoPoint(0))
    {
        vec3 posWorld;
        renderer::getWorldPosition(gotoPoint.data(), posWorld.data());
        if (posWorld(0) == posWorld(0))
        {
            double posNav[3];
            map->convert(posWorld.data(), posNav,
                         Srs::Physical, Srs::Navigation);
            map->setPositionPoint(posNav);
            map->options().navigationType = vts::NavigationType::Quick;
        }
        gotoPoint(0) = std::numeric_limits<double>::quiet_NaN();
        glBindFramebuffer(GL_FRAMEBUFFER, renderOptions.targetFrameBuffer);
    }

    mapRenderControls(view.contentScaleFactor, rect, _gestureViewLeft.frame, _gestureViewBottom.frame, _gestureViewRight.frame, _gestureViewCompas.frame);

    renderOptions.targetViewportX = rect.origin.x * view.contentScaleFactor;
    renderOptions.targetViewportY = rect.origin.y * view.contentScaleFactor;
    renderOptions.width = rect.size.width * view.contentScaleFactor;
    renderOptions.height = rect.size.height * view.contentScaleFactor;
    map->setWindowSize(renderOptions.width, renderOptions.height);
}

@end

