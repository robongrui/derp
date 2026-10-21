# Ragdoll motion: diagnosis and tuning research

19 October 2026. Research only; no engine, test or settings files changed.

The target is rough, violent Doom animation with convincing weight and contact. The priority subjects are the zombieman, shotgunner and imp. More force, more physically detailed simulation and more convincing violence are separate changes; the evidence below helps choose among them.

## Main diagnosis

The current system already launches chest deaths backward and upward. It also contains several mechanisms that restrain that motion, retain posture, terminate movement and hide intermediate poses. Some odd corpses can result from those controls, while others are configurations the anatomy and collision model permit. The final sprite camera adds another source of apparent pose changes.

The user's subsequent report of back-facing monsters turning toward the player reveals an earlier priority: preserve the transition from the living sprite to the puppet, then establish what the sprite camera can communicate. That turn is directly explained by the current source. The side-on presentation was introduced to compensate for unreadable feet-first bodies; it should be evaluated as a workaround for that problem. Sections 9–11 below cover the facing, knees and controlled projection comparison.

The motion priorities remain unfinished freezes, impact magnitude across response families, whole-body movement versus joint damping, and contact feedback. Anatomy and collision volume need attention where poses remain wrong after movement has actually settled.

This investigation identifies source-confirmed mechanisms and measurements from the existing Python lab. It does not identify every reported in-game corpse from a captured replay. The lab differs from the engine, so its failure counts are diagnostic evidence rather than an in-game failure rate.

## 1. The freeze rule can preserve unfinished movement

The engine stops after eight quiet landed tics, forty tics after the first landed event, or 175 total tics. The forty-tic condition has no requirement for a supported torso, acceptable anatomy or readable silhouette. The landed flag can begin with the head approaching the floor, before the main body comes down. See [landing detection](platform/scene3d/ragdoll.cpp) at `Ragdoll::steer` and the stopping condition in `ragdollView`.

The quiet metric sums squared stored point displacement. It measures motion, rather than contact quality or posture. A corpse can be quiet while supported by an implausible arrangement of joint spheres.

The existing settling lab was run without edits for three rigs, five falls and eight seeds: 120 falls. Seventeen reached the forty-tic cap; sixteen had not completed eight quiet tics, and nine still exceeded the motion threshold. Its printed timeout count was zero because it counts only cases with no recorded rest time. Forced freezing is counted as a successful rest.

Two zombieman examples had final motion metrics approximately 62 and 66 times the quiet threshold. In three in-memory diagnostic reruns, suppressing only the cap allowed natural sleep at 57, 52 and 72 tics after first contact. The knees moved another 9.24, 5.42 and 38.23 units. No source files were altered for these counterfactuals.

This demonstrates a mechanism for baking an unfinished pose in the lab. The appropriate next measurement is the stopping reason and contact state of actual bad in-game corpses. Simply extending the timer could reveal more jitter or a bad equilibrium; it is not established as a complete remedy.

## 2. Global damping removes motion that could carry the violence

Velocity is multiplied by 0.985 airborne and 0.9 while lying, four times per game tic. Doom runs at 35 tics per second.

| State | Velocity retained per tic | Velocity half-life |
| --- | ---: | ---: |
| Airborne | 94.13% | 328 ms |
| Lying | 65.61% | 47 ms |

Air drag alone removes about 65% of an unforced velocity in half a second. Once lying, the same damping acts on the whole doll, including limbs still above the floor. Floor contact also removes downward motion, with no restitution. These controls can suppress translation, rotation and limb follow-through together.

The calculation does not measure the full coupled body's loss of energy: gravity, constraints, steering and contacts also change motion. It does show that a larger initial push encounters strong attenuation before presentation.

The [original PBD paper, section 3.5](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf) describes damping internal deviations while preserving overall rigid translation and rotation. That suggests a relevant experiment: preserve the global flight and turn while damping unwanted internal oscillation separately. It does not prescribe a coefficient for this game.

Source: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `Ragdoll::tic`, `lying`, `ground`, `rub`.

## 3. Small correction coefficients are substantial in this solver

There are twenty-four constraint passes per tic. Permanent soft links between shoulders and hips and between head and shoulders keep a coefficient of 0.15 even when the temporary muscles have released. Post-contact extension uses 0.03 to open the legs and hips.

For an isolated link, repeated fractional correction would remove approximately 98% of its error per tic at 0.15, and 52% at 0.03. Coupled constraints and contact change the actual result, but these are meaningful strengths. The word soft in a comment is not a measurement of the resulting body flexibility.

The lab's final average leg reach was 93% of full length and torso reach 99% of standing length. No case met its narrow curled-pose criterion. This indicates that the existing anti-curl controls have substantial influence. They can still leave straight, braced or splayed shapes that that metric does not penalize.

Research on [XPBD](https://mmacklin.com/xpbd.pdf) explains why ordinary position-based correction strengths depend on timestep and iteration count. The immediate lesson is to tune these quantities together and evaluate their competing effects. A solver replacement is not yet justified.

Source: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), permanent `soft` links, `limits`, and the constraint loop in `tic`.

## 4. Pose categories discard much of the impact magnitude

Head detection has first priority, then low or weak hits, then off-centre hits, then chest. The force multiplier enters the chest impulse. The head, leg and side impulses are largely fixed. The retained global launch also applies only to chest deaths.

A strong head or side hit can therefore select a different collapse without carrying the stronger impact into its body motion. This is a concrete reason that increasing one chest-launch parameter cannot improve every common death.

The ragdoll's force input is the change in gameplay momentum after expected friction. Damage values are discarded by the impact collectors, pellets within a tic are averaged without damage weighting, and weapon identity is not passed into the ragdoll. Gameplay momentum does carry some damage information through Doom's thrust calculation; it is not a dedicated artistic impact description.

The current chest launch at `hard = 1` injects 2.5 units/tic backward, 2 upward, and a turn. As a scalar illustration, an isolated point under the actual gravity and air damping rises about five units, reaching its apex near tic 5.5. At the capped stronger lift of three units/tic, the rise is about 10.4 units. These are calculations for an isolated point, not measurements of the articulated body. The violence must also read through rotation, sideways separation, timing and the landing.

The Reaction strength slider affects the older body-reaction path. It does not scale the active ragdoll. This is important if the slider has been used to judge how much stronger the system can become.

Sources: [hit collection](platform/scene3d/body_motion.cpp), `bodyHit`, `bodyDamaged`, `pushOf`; [response selection](platform/scene3d/ragdoll.cpp), `startRagdoll`, `launch`; [settings](platform/settings_panel.cpp), `body_motion_strength`.

## 5. The visible sprite sequence can miss the important action

The first pose is published after one simulated tic. Later poses normally hold for five tics on the zombieman and eight on the imp: about 143 and 229 milliseconds. Landing forces a pose update, but recoil extrema, launch apex and successive hip, chest and head contacts do not each receive an explicit sample.

A brief recoil can happen inside a held image. Increasing the impulse may change the hidden trajectory without delivering a clear additional pose. Equally spaced holds can also make different stages feel similarly paced, despite a desire for an abrupt hit followed by a fast collapse.

The appropriate experiment preserves stepped sprite animation and compares the same trajectory through ordinary holds and selected impact/extreme/contact poses. It does not require rendering a smoothly changing body every display frame.

[Lasseter's original animation paper](https://users.cs.northwestern.edu/~animation/Lasseter_1987.pdf) connects timing, staging, overlap and exaggeration to the communication of weight. Its relevance here is the selection and duration of visible actions. It gives no ragdoll parameter values and does not establish that any particular hold scheme will succeed.

Sources: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `poseTics`, `ragdollView`; [lab timing](tests/death_ragdoll.py), `STEP`, `engine_poses`.

## 6. Anatomy and contact permit bad equilibria

The knee's wrong-way test checks one component of a cross product. It is not a complete hinge-plane constraint. Hip movement lacks explicit bounds for every sideways and twisting degree of freedom. When the knee fails the sign test, it is rotated halfway around the hip-to-ankle line. Transforming its previous position preserves speed magnitude, but the visible pose correction is discontinuous.

The lab recorded 107 post-contact knee flips over 120 falls. Some knee movement is legitimate settling; the flips show that the current correction still matters after first contact.

Collision uses eleven joint-centred spheres. The limb segments, visible relief thickness and held weapon have no full collision representation, and limbs do not collide with one another. A segment can cross a stair edge or another limb while its endpoint spheres are valid. Friction changes cannot fully repair that contact geometry.

[Jakobsen's Hitman account](https://www.cs.cmu.edu/afs/cs/academic/class/15462-s13/www/lec_slides/Jakobsen.pdf) is particularly relevant: the simple particle ragdoll also used anatomical restrictions, inequality constraints to keep limbs apart, and capped-cylinder limb contacts. These are compatible with a small hidden puppet. A highly detailed anatomical model is unnecessary to test their value.

Equal endpoint corrections also mean there is no differentiated inverse-mass weighting. Mass distribution is worth a later controlled comparison, with weaker direct evidence than premature freezing, incomplete joint bounds and sparse contact.

Source: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `radius`, `link`, `limits`, `ground`, collision loop in `tic`.

## 7. Projection can make a physically acceptable pose look wrong

The virtual camera now progressively turns toward a side-on view as the body lies down, then quantizes the heading to 45-degree intervals and pitch to five degrees. The silhouette changes through both skeleton motion and view changes. A step in view can look like a sudden body turn or altered limb arrangement.

The final baked image fixes the view chosen at the first resting draw. It cannot later correct an unlucky projection. The same picture is reused from every viewing direction.

All ordinary monster silhouette-shadow paths are skipped for ragdolls. Sprite grounding darkening remains, but a detached shadow is unavailable as a cue to launch height or contact. This is a plausible perceptual contributor, rather than a measured cause of the user's specific examples.

A diagnostic should compare the same skeleton through a fixed camera and the adaptive sprite camera, with collision shapes visible separately. Every rejected corpse should retain its actual baked image as well as its skeleton pose.

Sources: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `ragdollCard`; [geometry.cpp](platform/scene3d/geometry.cpp), ragdoll shadow exclusions.

## 8. Landing effects can emphasize the wrong instant

The first sticky landed event emits the floor burst. It can be triggered by head proximity while the chest is still elevated, but the effect is placed on the floor under the spine. A later torso impact cannot trigger the same event again.

The additional body slap is emitted only when a chest launch was scheduled. The landing function receives a thrown/not-thrown flag, without incoming normal speed, contact part, impulse or mass. Small and large launched impacts therefore use the same basic effect scale. Strong unlaunched head and side falls receive no floor slap through this hook.

Sound is coupled to the spray setting and is suppressed along with it, and on liquid floors. A separate wall-slam hook follows deceleration of the gameplay object rather than contact of the local ragdoll skeleton. Existing death sounds and other audio still operate; this finding concerns the added body-impact feedback.

This is a strong candidate for weak impact punctuation. Separating first touch, major torso arrest and later limb contact would allow presentation to follow actual movement. Louder audio alone would retain the timing mismatch.

Sources: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `steer`, landing branch in `ragdollView`; [spray.cpp](platform/scene3d/spray.cpp), `sprayLanding`, wall-slam handling; [i_sdl_sound.c](platform/i_sdl_sound.c), `I_MadeSound`.

## Limits of the current diagnostics

The existing [settling lab](tests/ragdoll_settle.py) never applies the launch stored in the chest preset. It does not exercise guided falls, wall contact, gameplay-object movement or the runtime camera. Its random sequence differs from the engine's pointer/time hash. Its leg-reach correction also has two independent tests where the engine has an if/else-if branch. Its default imp input is the attack image `TROOG1`; the current runtime uses the pain image `TROOH1`. The controlled projection study in section 11 uses that current pain image.

The visual lab does apply launch, but its general view uses three-tic pose holds. Its guided engine-pose comparison uses the zombieman's five-tic duration. Neither is a complete reproduction of the imp's runtime presentation or the adaptive side view.

The earlier paper's appendix predates the current launch and camera changes. It should not be used as the exact specification of today's motion.

## Recommended order of experiments

1. Establish matching diagnostic inputs and record stopping reasons. Keep source art, seed, hit, camera and environment fixed. Capture the skeleton, collision volumes and exact sprite sequence together.
2. Resolve the facing transition in the diagnostic design. Compare the last living rotation, intended body heading and first puppet image for front, rear and side kills. The present observer-facing reset confounds later judgments about impact direction.
3. Compare fixed poses through different virtual views, with the adaptive side-view rule disabled only in the diagnostic. Include textured and silhouette images at native sprite resolution. Establish whether each failure comes from articulation, projection or missing surface information.
4. Classify each ugly corpse: unfinished freeze; invalid anatomy; false support/penetration; poor projection; or a stable but artistically weak pose. Each requires a different intervention.
5. Compare ordinary freezing with enough continued simulation to establish whether the pose improves or remains wrong. Keep impact strength unchanged.
6. Separate whole-body travel and turn from internal damping. Compare recoil and airborne motion before increasing forces.
7. Preserve strength in chest, leg, side and head response families. Judge a pistol and shotgun separately even when both select the same collapse type.
8. Compare ordinary pose holds with event-selected stepped poses. Protect the first hit, a useful extreme, the main ground impact and a short secondary settle.
9. Improve anatomical bounds and collision volume where supported failures persist. Review additional masses only after the more direct causes are isolated.
10. Align landing blood and sound with significant contact. Evaluate shadows as a separate weight cue, with audio and spray held constant in motion comparisons.

The initial comparison set should cover the three common monsters with low, central, lateral and head hits. Use a small fixed seed set. Start on a flat floor, then take the failing cases to a wall, stair edge and ledge. Keep the original sprite pipeline and judge the delivered images at gameplay scale.

Score initial punch, readable collapse, anatomical plausibility, visible support and final silhouette separately. A single average realism score would hide the artistic target. The desired sequence has a sharp interruption, asymmetric loss of support, a committed fall, a decisive main contact and limited secondary motion.

## Additional primary sources and their limits

- [Zordan and Hodgins, Motion Capture-Driven Simulations that Hit and React (2002)](https://publications.ri.cmu.edu/motion-capture-driven-simulations-that-hit-and-react): timed regional control strength helps distinguish tracking a living pose from yielding to impact. The full motion-capture controller is much larger than this project's need.
- [Macklin et al., Small Steps in Physics Simulation (2019)](https://mmacklin.com/smallsteps.pdf): in their tested XPBD systems, smaller substeps reduced error and numerical damping. Changing substeps here would also change the accumulated explicit damping and constraint response, so it needs a controlled comparison rather than a direct copy of their settings.

The first changes eventually chosen should address observed mechanisms. The research does not support replacing the hidden-puppet pipeline, restoring per-frame smoothness, importing character models or adding a general-purpose full ragdoll engine as an initial response.

## 9. The facing setting is bypassed by the ragdoll and procedural fall

The reported turn toward the player has a source-confirmed cause. `startDeath` returns after a successful ragdoll or procedural fall before reaching the `deathFaceViewer` condition. The setting governs the older director path. Its UI description does not explain this restricted scope.

`startRagdoll` initially derives body axes from `thing.angle`, then unconditionally replaces them with axes facing the display player. The puppet is built from the front pain sprite. A rear-facing living image is therefore replaced with a front-derived body facing the observer, independent of the setting. This is a visual reorientation; this code does not change the gameplay object's angle.

The reset also affects hit interpretation. The incoming world direction is transformed into those new axes. A shot into the monster's back can become a front-to-back chest response in puppet coordinates, and can qualify for the front-facing guided chest fall. Observer direction and anatomical facing are conflated before the simulation starts.

Returning the original angle through `ragdollView` does not restore the appearance. Active falls suppress ordinary directional sprite selection. The procedural fallback likewise uses the front drawing and deforms it in the camera's plane, without preserving a world-space body heading.

Preserving the axes alone would expose another appearance problem: the relief's front and back use the same front-image colour map. The side image supplies depth, rather than side colours. There is no rear texture selected from the monster's rear artwork. A coherent repair would have to consider both heading and the visible source image. This is a requirement for a future experiment, not a code change made during this investigation.

Sources: [death_director.cpp](platform/scene3d/death_director.cpp), lines 89–92 and 137; [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `reliefOfState`, `startRagdoll`, `ragdollView`; [ragdoll_body.cpp](platform/scene3d/ragdoll_body.cpp), colour map and front/back face construction; [death_falls.cpp](platform/scene3d/death_falls.cpp), `lumpOf`, `fallView`, `fallQuads`; [geometry.cpp](platform/scene3d/geometry.cpp), `rotationFrom`.

## 10. Bent knees have several separate causes

The leg reach limits allow substantial knee flexion. For equal thigh and shin lengths, with flexion zero at a straight knee, the relationship is:

`flexion = 2 acos(hip-to-ankle distance / full leg length)`

The 55% minimum reach permits about 113° of flexion. The weak lying-body correction toward 85% reach still permits about 64°. These numbers illustrate the geometry of equal segments; the actual angle for each rig depends on its thigh and shin lengths. Reaching 85% does not make a leg visually straight.

There are deliberate bending inputs too. The general response gives both knees an initial forward kick. The guided zombieman response can kick them again when support gives way, and its later reference keys contain 80° and 95° knee bends. These are intermediate collapse poses. If damping, support or the stopping rule retains part of that shape, a flexed final corpse follows naturally.

The existing knee correction controls the sign of one cross-product component. It lacks a complete hinge-plane constraint. Sparse joint-sphere contacts and the absence of limb-to-limb collisions permit additional configurations a human observer rejects. These mechanisms were covered in section 6.

A follow-up measurement of the same 120 legacy lab falls separated bend magnitude from bend direction. Among 240 legs, 104 ended with a thigh-to-shin bend over 45°. Of those, 101 had a bend axis whose absolute alignment with the pelvis's lateral axis was below 0.5, and 93 had their knee sphere within half a unit of the floor. In this proxy, these are predominantly bends outside the intended knee plane. For example, one zombieman side-hit result had a 62° bend, almost zero axis alignment and a knee sphere resting on the floor. A wrong-way sign test can accept this because the tested cross-product component is near zero.

The measurement uses the normalised hip-to-knee and knee-to-ankle directions, their normalised cross product as the bend axis, and the hip-to-hip direction as the proxy's intended hinge axis. A full anatomical model would allow thigh axial rotation to change the knee's plane; this rig does not represent that degree of freedom separately. These counts describe the legacy lab, with the parity limitations above, rather than the frequency of bad current-engine knees. They identify a concrete constraint/contact mechanism to inspect in captured failures.

Projection adds apparent bending: feet, shins, thighs and pelvis can overlap along a feet-first line of sight. A compact image cannot establish whether the actual knee is over-flexed. The diagnostic should retain the joint angle and a second view of the same pose, rather than infer anatomy from the final card alone. Some flexion is appropriate for a dead body; making every leg straight would create another repeated pose.

Source: [ragdoll.cpp](platform/scene3d/ragdoll.cpp), `limits`, `guideKeys`, `guideBefore` and the initial knee pushes in `startRagdoll`.

## 11. Camera elevation cannot recover the original corpse composition by itself

A [controlled projection sheet](ragdoll-camera-study.png) compares the zombieman, shotgunner and imp. Each row contains its original pain image, original final corpse drawing, and the same relief rigidly rotated onto its back. There is no simulated collapse or adaptive camera in this study. Four views isolate feet-first elevation from oblique and side views.

All images are rasterised at one pixel per unit and displayed at four times that size with nearest-neighbour scaling. Relief geometry comes from the existing Python lab with the front image supplied on both sides, and uses runtime-like rest-surface dimming through the WAD's colormap. The sheet is a controlled illustration, rather than an exact engine capture: it omits dynamic lighting, wounds, contact, physics and the current adaptive camera. Its input frames are `POSSG1`, `SPOSG1` and `TROOH1`; its corpse references are `POSSL0`, `SPOSL0` and `TROOM0`.

| Subject | Original corpse bounds | Flat feet-first, pitch 30° | Flat feet-first, pitch 60° | Flat side view, pitch 30° |
| --- | --- | --- | --- | --- |
| Zombieman | 47 × 17 | 39 × 30 | 39 × 48 | 55 × 21 |
| Shotgunner | 52 × 17 | 31 × 30 | 31 × 48 | 55 × 22 |
| Imp | 58 × 22 | 41 × 29 | 41 × 49 | 55 × 21 |

These are opaque image bounds, not skeleton dimensions or a quality score. The images make the more useful point: even this nearly straight, flat body can look like a shortened upright character from the feet. Increasing elevation restores more of the original standing image and makes the card taller. The side view restores a wide, horizontal body silhouette. The 45° view is an intermediate case, which warrants testing before assuming a full side view is necessary.

For a horizontal body aligned with an orthographic camera's horizontal look direction, its longitudinal contribution to screen height is `L sin(pitch)`. At 30° it contributes only half its length; at 60° about 87%. This is vertical extent on the eventual upright billboard. More elevation therefore does not automatically produce a flatter corpse image. At a top-down view, a face-up relief can recover much of its original frontal appearance and then stand that image upright on the card.

The original corpse drawings are separate images. In the three inspected examples, their main forms are spread laterally and their surfaces, damage and limb arrangement differ from the pain drawing. The generated relief retains the standing image's face, clothing, gun and shading while revealing inferred volume around them. It has no independently articulated feet or arms and no corresponding painted soles or rear surface. A different camera can change occlusion; it cannot supply missing anatomy and surface information.

This limitation is consistent with graphics research. [Rademacher's View-Dependent Geometry (1999)](https://www.cs.princeton.edu/courses/archive/fall00/cs597b/papers/rademacher99.pdf) preserves view-specific artistic distortions through changes in geometry. [Jain et al., Three-dimensional Proxies for Hand-drawn Characters (2012)](https://graphics.cs.cmu.edu/?p=256) explain the ambiguity of inferring depth from drawings and the deliberate geometric inconsistencies of hand animation. These papers support investigating view-dependent presentation; they do not diagnose Doom's art or establish an automatic solution for this renderer.

The camera currently depends on torso tilt and the head-to-ankle axis. It does not inspect visible head area, torso exposure, limb overlap or the native-resolution silhouette. Its side-view choice is quantised in 45° steps. Switching the selected view can change the image while the skeleton changes very little, and the image is frozen after rest. Its actual viewing elevation also comes from the pose rather than the player's height above the body.

Camera work is warranted. A useful experiment would hold a bad world pose fixed, search a bounded range of nearby yaw and pitch values, and compare the textured sprite with a silhouette and a skeleton view. Keep the camera stable across adjacent poses and record how much the displayed direction departs from the player's view. Zoom alone cannot separate occluded parts. Perspective can enlarge foreground feet relative to the torso, so it is not an established remedy.

If modest, consistent view changes solve many cases, camera calibration has substantial value. If only a 90° orbit makes the body readable, the result documents the workaround's cost. If an anatomically sound pose remains ambiguous across reasonable views, rendering-only changes to part separation, surface sourcing or view-dependent shape become the next research direction. Such adjustments should preserve the physical contact and hit response rather than forcing every death into a sideways fall.
