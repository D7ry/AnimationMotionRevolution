# AMR 动画与行为开发者指南

> **适用对象：** 本文档面向动画作者和 Behavior（行为图）开发者，说明此 Animation Motion Revolution（AMR）分支实际实现的动画注释与图变量契约。本文不是面向普通玩家的安装教程。

语言：[English](developer-guide.md) | **简体中文** | [한국어](developer-guide.ko-KR.md)

## 1. AMR 控制什么

AMR 读取 HKX 动画内嵌的注释，并可用下列数据替换该 Clip 的运动数据：

- `animmotion`：累计局部位移；
- `animrotation`：累计偏航旋转；
- `animwarp` / `animwarpend`：可选，按时间段把水平 `animmotion` 缩放至角色的当前战斗目标。

行为图必须实际使用动画驱动运动。请在相应 Behavior/State 中正确配置动画驱动与旋转权限（`bAnimationDriven` 和/或 `bAllowRotation`）。如果行为本身忽略根运动，仅添加注释并不能让角色移动。

AMR 对 `RE::Character` 实例工作，因此玩家、NPC 以及受支持的生物共用同一运行逻辑。但具体行为项目仍须暴露并使用相应运动数据与图变量。

## 2. 注释格式与通用解析规则

hkanno 风格的一行由注释时间戳和注释文本组成：

```text
0.500000 animmotion 0 100 0
0.500000 animrotation 45
0.200000 animwarp 0 1 60 300
0.750000 animwarpend
```

时间戳由 Havok 独立保存，单位是从 Clip 开始计算的秒数；其后的文字才是 AMR 解析的内容。

请遵守以下编写规则：

- 注释名区分大小写且必须为小写：严格使用 `animmotion`、`animrotation`、`animwarp`、`animwarpend`。
- AMR 注释名之前不要放空白字符。
- 数值之间使用普通 ASCII 空格。当前解析器要求 `animmotion` / `animrotation` 名称后必须有一个字面空格；`animwarp` / `animwarpend` 也接受 Tab，但不建议依赖这种差异。
- 数值必须是有限、使用小数点的浮点数。底层解析器支持科学计数法；`NaN` 和无穷大无效。
- `animmotion` 与 `animrotation` 的时间戳应保持在 `[0, Clip 时长]` 内。与扭曲控制不同，运动和旋转时间戳不会被自动限制到有效范围。
- `animmotion` 与 `animrotation` 的每个 Key 使用唯一时间戳；相同时间的重复 Key 顺序没有定义。
- 同一 Clip 的所有 `animmotion` 与 `animrotation` Key 必须放在同一个 Annotation Track。AMR 只使用第一个含有任意有效运动或旋转 Key 的 Track，后续 Track 中的运动/旋转 Key 会被忽略。
- 扭曲控制及自动战斗分段事件会扫描全部 Annotation Track。

当前解析器会忽略有效 `animmotion` 或 `animrotation` 后面多余的值。这只是实现细节，不属于推荐语法。相反，`animwarp` 或 `animwarpend` 多出参数会被判定为格式错误。

格式错误的运动/旋转注释会被静默忽略；使用了精确保留 Token 但格式错误的扭曲控制会写入日志，并触发[显式控制与失败关闭](#显式控制与失败关闭)中的行为。

## 3. `animmotion`：累计位移

### 语法

```text
animmotion <x> <y> <z>
```

三个值代表**该注释时间点的累计局部位移**，不是在该 Key 额外增加的位移。

- X：横向移动；
- Y：前后移动；按照 Skyrim/ADSF 标准约定，正 Y 是角色面朝方向；
- Z：垂直移动。

数值采用 Skyrim 动画/世界单位比例，与 `animationdatasinglefile.txt`（ADSF）的位移约定相同，并非 Havok 米制单位。允许负数。

示例：

```text
0.000000 animmotion 0 0 0
0.250000 animmotion 0 20 0
0.500000 animmotion 0 80 0
0.750000 animmotion 0 100 0
```

0.25 秒时累计 Y 为 20；下一条表示 0.50 秒时累计 Y 为 80，因此这段时间实际贡献 60 个单位。AMR 在 Key 之间对每个分量做线性插值。

### 起点与终点 Key

推荐格式：

```text
0.000000 animmotion 0 0 0
<动画时长> animmotion <最终-x> <最终-y> <最终-z>
```

- 如果第一条 Key 晚于 0 秒，AMR 会从 0 秒的隐式零向量插值。
- 如果 0 秒 Key 本身不是零，它会成为立即生效的累计基线，并且不会被运动扭曲缩放；这可能表现为跳变，因此通常应从 `0 0 0` 开始。
- 最后一条 Key 之后会保持最终累计值。若最后一条自定义运动时间与 HKX 时长不同，AMR 会记录警告。
- `animmotion` 曲线末尾（不一定等于 HKX 声明时长）也是运动扭曲时间线的末尾。

存在有效 `animmotion` 时，它会替换该 Clip 的原版平移运动。不存在时，AMR 让游戏使用可用的原版平移；自定义与原版平移都不存在时，采样结果为零。

## 4. `animrotation`：累计偏航角

### 语法

```text
animrotation <偏航角度>
```

`animrotation` 定义绕垂直 Z 轴的累计偏航，不包含俯仰和翻滚。AMR 把每个角度转换成四元数，并使用游戏的四元数插值处理 Key 之间的旋转。允许负角度。

完整旋转示例：

```text
0.000000 animrotation 0
0.500000 animrotation 90
0.900000 animrotation 180
1.200000 animrotation 270
1.500000 animrotation 360
```

接近或超过 180 度的旋转必须使用中间 Key。单独一条 `animrotation 360` 的四元数朝向等同于 0 度，无法自行描述完整旋转路径。

与位移一样，建议从零开始，并把最终 Key 放在 Clip 末尾。存在有效自定义旋转曲线时，它会替换该 Clip 的原版旋转；不存在时保留可用的原版旋转；两者都不存在时使用单位旋转。

`animmotion` 和 `animrotation` 可以混合放在同一 Annotation Track。运动扭曲永远不会修改 `animrotation`。

## 5. `animwarp` 与 `animwarpend`

运动扭曲会统一缩放某段的 X/Y 位移；它不会创建运动、旋转路径、把角色转向目标、缩放 Z 或修改 `animrotation`。因此，没有有效 `animmotion` 位移曲线的 Clip 无法由 AMR 执行运动扭曲。

### 语法

```text
animwarp <最小倍率> <最大倍率> [最大角度] [最大距离]
animwarpend
```

参数按位置解释：

- 最小倍率：允许的最低 X/Y 倍率，必须不小于 0；
- 最大倍率：允许的最高 X/Y 倍率，必须不小于最小倍率；
- 最大角度：可选，0–180 度的方向门限，默认 60；
- 最大距离：可选，角色到目标的非负水平距离上限，使用 Skyrim 单位，默认无限制。

若要填写距离，必须先填写角度。

示例：

```text
animwarp 0 1
animwarp 0.25 1
animwarp 0 1.25
animwarp 0 1.5 60 300
animwarpend
```

`animwarp 0 1` 允许缩短到完全停止，但不允许延长。最大倍率大于 1 时允许延长。最小倍率大于 0 时，即使角色已处于全局停止距离内，也不会完全停止。

`animwarpend` 从该时间点起切换到非启用状态，直到后面出现新的有效 `animwarp`。`animwarp 1 1` 即使带有有效的可选角度或距离参数，也会被规范化为完全相同的非启用状态；无效的可选值仍会使整条注释格式错误。

### 显式分段

每条有效控制拥有从自身时间点开始的半开区间：

```text
[当前控制时间, 下一控制时间)
```

最后一条控制结束于最后一个 `animmotion` 时间点。后一条控制的精确时间属于新段。如果单次游戏更新跨过一个或多个边界，AMR 会拆分该更新，并分别用各段规则处理相应部分，避免丢失一帧位移或人为停顿。

示例：

```text
0.200000 animwarp 0 1 45 250
0.600000 animwarp 0.5 1.5 90 400
1.000000 animwarpend
1.400000 animwarp 0 1
```

结果：

- `[0.00, 0.20)`：不扭曲；
- `[0.20, 0.60)`：倍率 0–1，45 度，250 单位；
- `[0.60, 1.00)`：倍率 0.5–1.5，90 度，400 单位；
- `[1.00, 1.40)`：不扭曲；
- `[1.40, animmotion 末尾)`：倍率 0–1。

超出位移曲线范围的控制会被限制到曲线范围并记录警告。同一时间戳上，以 Annotation Track 遍历顺序中最后遇到的控制为准；这种顺序很容易被误解，应避免同时间控制。

### 显式控制与失败关闭

只要出现任意精确保留的 `animwarp` 或 `animwarpend` Token——无论格式正确与否——整个 Clip 都视为显式控制：

- 整个 Clip 不再使用隐式攻击扭曲；
- 第一条有效 `animwarp` 以前不扭曲；
- `animwarpend` 后保持不扭曲，直到后面出现有效 `animwarp`；
- 有效显式段即使在角色不处于攻击状态时也可以扭曲；
- INI 的 `bEnableForAttackAnimations` 不会关闭有效显式段。

格式错误的精确控制不会增加分段标记，但仍会关闭默认攻击扭曲，并在 `AnimationMotionRevolution.log` 中产生警告。这样可以防止拼写错误意外退回隐式战斗逻辑。

以下控制无效：

```text
animwarp -1 1
animwarp 1 0.5
animwarp 0
animwarp 0 1 181
animwarp 0 1 60 -1
animwarp 0 1 60 300 extra
animwarpend extra
```

## 6. 攻击动画的隐式扭曲

当 Clip 有有效 `animmotion`、没有任何保留的扭曲控制，并且 INI 开启默认攻击扭曲时，AMR 会生成隐式战斗段。它们只在角色报告 `IsAttacking()` 时生效。

随附默认值等价于：

```text
0.000000 animwarp 0 1 60
```

实际倍率与角度来自 INI。隐式扭曲没有最大距离门限。

AMR 会在下列事件处结束当前隐式段并立即开始新段；这些事件会在全部 Annotation Track 中搜索：

- 大小写不敏感的精确 `HitFrame`，允许点号载荷，例如 `HitFrame.$payload`；
- 注释文本以 `Collision_Add` 开头（大小写不敏感），例如 `Collision_Add.Node(WEAPON)`。

检查这两种边界事件时会忽略开头的空格/Tab。`preHitFrame`、`NPCHitFrame`、`2_HitFrame` 不匹配。当前 `Collision_Add` 是原始前缀判断，因此 `Collision_Additional` 也会匹配；请只把此前缀用于预期的碰撞边界。

示例：

```text
0.000000 animmotion 0 0 0
0.700000 HitFrame
1.100000 Collision_Add.Node(WEAPON)
1.500000 animmotion 0 180 0
```

隐式时间线为 `[0, 0.7)`、`[0.7, 1.1)`、`[1.1, 1.5)`。每段都会重新计算倍率，使连续命中可以分别对齐当前目标。同一时间或相差不超过 `0.0001` 秒的边界会合并。

## 7. 扭曲计算与目标规则

正常进入一段时，AMR 会在两个精确边界采样累计位移：

```text
原始向量 = animmotion(段末) - animmotion(段首)
原始距离 = lengthXY(原始向量)
期望距离 = max(0, 目标水平距离 - fStopDistance)
请求倍率 = 期望距离 / 原始距离
最终倍率 = clamp(请求倍率, 最小倍率, 最大倍率)
```

Clip 激活时会缓存边界位移，因此计算为常数时间。如果一段开始以后扭曲才首次变得有效——例如刚取得/更换目标、重新进入距离范围或进入攻击状态——AMR 只使用当前游标到同一段末的**剩余**累计向量。

只有所有适用条件均通过时，扭曲才算“已应用”：

- 角色拥有可解析的 `currentCombatTarget`；
- 目标存活；
- 角色与目标在同一 Cell；
- 当前水平距离不超过显式最大距离；
- 剩余原始水平距离不少于 `fMinimumAuthoredDistance` 且不为零；
- 原始世界空间向量到目标向量的夹角不超过门限；
- 仅对隐式段：默认攻击扭曲已开启，且角色正在攻击。

角度门限比较的是**原始运动方向与目标方向**，不是角色朝向与目标方向。因此，目标在身后时，如果该段原始运动也向后，仍可能扭曲；相反，AMR 绝不会把向前的段旋转到身后的目标。

角色与目标的水平距离使用二者 Reference Position；不会减去碰撞胶囊半径。若要保持间隔，请使用 `fStopDistance`。

倍率计算后会针对同一目标和同一段缓存。AMR 不会为了追随移动目标而每帧重新计算倍率。目标有效性、同 Cell、最大距离及隐式攻击状态可以关闭扭曲；重新满足条件时会根据剩余运动计算。缓存倍率仍有效时不会重新检查角度。进入新段必定重新计算。

段距离是**净位移**的长度，而不是沿曲线实际行进的路径长度。同一段内的每个 X/Y 增量使用同一倍率。如果动画先后向后、向前，请在需要独立处理的方向变化处添加控制边界。过大的最大倍率会放大段内全部水平运动，可能产生滑步或不自然的高速突进。

## 8. 行为图变量

AMR 为行为作者发布以下布尔图变量：

```text
AMR_IsAnimationWarpingEnabled
```

其精确运行含义为：

- `true`：该角色至少有一个活跃的 AMR 位移 Clip 当前**实际应用**了运动扭曲；
- `false`：该角色没有任何活跃 AMR 位移 Clip 正在应用扭曲。

“已应用”比“Clip 含有 `animwarp`”更严格。无有效目标、门限失败、剩余原始运动不足、`animwarpend` 非启用区间、未开启/未攻击的隐式规则，或没有 `animmotion` 时均为 false。有效计算即使最终倍率恰好为 1，也仍为 true。

AMR 会汇总同时活跃的已跟踪 Clip，因此一个未扭曲的混合 Clip 不会把另一个正在扭曲的 Clip 的 true 覆盖掉。AMR 在自定义位移采样时重新写入汇总值，并在已跟踪 Clip 停用时清除或重新计算。

发布包包含以下 Behavior Data Injector 配置：

```text
Data/SKSE/Plugins/BehaviorDataInjector/AnimationMotionRevolution_BDI.json
```

等价 JSON：

```json
[
  {
    "projectPath": "Actors",
    "type": "kBool",
    "name": "AMR_IsAnimationWarpingEnabled",
    "value": false
  }
]
```

`projectPath: "Actors"` 会让 Behavior Data Injector 把初始值为 false 的变量递归加入 `Data/Meshes/Actors` 下的 Behavior 项目。请安装兼容的 Behavior Data Injector 及其对应运行时支持。此 JSON 注入不需要运行 Nemesis 或 Pandora。缺少被注入变量时，AMR 根运动与扭曲仍会执行，但行为图无法可靠读取此自定义变量。

## 9. 悬崖边缘保护

悬崖保护由 INI 控制，没有直接启用它的动画注释。它在运动扭曲之后执行，只要满足以下条件，前后左右任意水平方向都可被限制：

- Clip 有自定义 `animmotion` 位移；
- `EdgeProtection.bEnableForAttackAnimations` 为 true；
- 角色报告 `IsAttacking()`；
- 整条 `animmotion` 曲线没有任意 Key 含明显非零 Z 值；
- 当前水平步长不少于 `fMinimumHorizontalDelta`。

自定义曲线中任意非零 Z Key 都会为整个 Clip 禁用边缘限制，因此向上跳或向前上方跳会保留原始运动。AMR 不会为这项判断检查原版位移数据。

对符合条件的步长，AMR 会：

1. 预测当前扭曲后 X/Y 步长结束时的角色中心；
2. 在可用时从角色 Havok Character Controller 的凸形状获取水平占地半径；
3. 沿预期移动方向，把探测点从预测中心移到 Controller 边界；
4. 使用角色碰撞组与 Character Controller Layer 沿世界 Z 轴垂直向下射线检测；
5. 如果没有命中支撑，则禁止该帧 X/Y 位移。

被禁止的位移会被消耗而不是累积，因此回到有效地面时不会发生补偿性跳跃。Z 保持不变。如果没有可用 Havok World，检测会失败放行，不阻挡运动。任意可接受射线命中都算支撑；系统不会判断材质、坡度或 Navmesh 可行走性。

## 10. 配置默认值

`Data/SKSE/Plugins/AnimationMotionRevolution.ini` 随附：

| Section/Key | 默认值 | 含义 |
| --- | ---: | --- |
| `MotionWarping.bEnableForAttackAnimations` | `true` | 开启隐式战斗段；有效显式段不受此开关影响。 |
| `MotionWarping.fDefaultMinimumScale` | `0.0` | 隐式 X/Y 最小倍率。 |
| `MotionWarping.fDefaultMaximumScale` | `1.0` | 隐式 X/Y 最大倍率。 |
| `MotionWarping.fDefaultMaximumAngleDegrees` | `60.0` | 隐式方向门限。 |
| `MotionWarping.fStopDistance` | `0.0` | 全局目标水平间隔，对显式和隐式扭曲均生效。 |
| `MotionWarping.fMinimumAuthoredDistance` | `1.0` | 执行扭曲所需的最小剩余原始水平距离。 |
| `EdgeProtection.bEnableForAttackAnimations` | `true` | 开启攻击悬崖保护。 |
| `EdgeProtection.fRaycastStartHeight` | `50.0` | 射线起点在预测边界上方的 Skyrim 单位数。 |
| `EdgeProtection.fRaycastDownwardRange` | `200.0` | 从抬高后的起点向下检测的射线长度。 |
| `EdgeProtection.fMinimumHorizontalDelta` | `0.10` | 触发探测的最小单帧步长。 |
| `EdgeProtection.bDebugDraw` | `true` | 允许可选 TrueHUD 边缘调试显示。 |

增大向下距离会接受角色下方更远的支撑，因此更容易走出/落下边缘；设置太小则可能在较小高度差处错误阻挡角色。

## 11. 可选 TrueHUD 开发调试显示

只有使用 `AMR_ENABLE_TRUEHUD_DEBUG` 编译的版本才包含 TrueHUD 调试功能。宏关闭时，AMR 完全不编译或包含 TrueHUD API 集成。两个显示初始均隐藏，热键不会改变游戏逻辑：

- **Page Up：** 切换运动扭曲快照；
- **Page Down：** 切换悬崖保护探测。

当显式段激活，或攻击扭曲符合条件时隐式段激活，扭曲显示会绘制一次持续 1 秒、固定在世界空间的快照。正在运行的合格段若因目标、攻击、角度或距离条件后来才首次应用，也可再绘制一次。

- 黄色：完整原始段向量；
- 青色（抬高 8 单位）：实际应用扭曲时的缩放后段预测；
- `animwarpend` 等非启用区间：不绘制扭曲快照。

快照使用绘制瞬间的角色位置与朝向，之后不会跟随角色转向；显示的是整段向量，不是不断缩短的剩余向量。

边缘显示会在每次符合条件的探测时刷新：黄色表示预测中心到 Controller 边界，绿色表示命中支撑的射线，红色表示未命中。探测图元只短暂保留，因此连续检查看起来是连续的。

## 12. 完整编写示例

```text
# 时间戳       注释文本
0.000000     animmotion 0 0 0
0.000000     animrotation 0
0.150000     animwarp 0 1.20 60 350
0.350000     animmotion 0 45 0
0.500000     animrotation 15
0.700000     animmotion 0 110 0
0.700000     animwarpend
1.000000     animmotion 0 135 0
1.000000     animrotation 0
```

- 位移和旋转都是累计值。
- `[0, 0.15)` 使用自定义运动但不扭曲。
- `[0.15, 0.70)` 在目标门限通过时可把 X/Y 缩放至 0%–120%。
- `[0.70, 1.00)` 使用自定义运动但不扭曲。
- Z 与原始旋转永远不会被 `animwarp` 缩放。
- 因为此 Clip 含显式扭曲控制，所以它绝不会接受隐式攻击分段。

扭曲控制时间不必和运动 Key 重合；AMR 会在精确控制时间插值并缓存累计位移。

## 13. 作者检查清单与限制

发布动画前：

1. 确认 Behavior 会使用动画驱动的位移/旋转。
2. 把全部 `animmotion` 与 `animrotation` Key 放在同一 Annotation Track。
3. 使用小写名称、ASCII 空格、有限数值及唯一时间戳。
4. 把 XYZ 与偏航角当作累计值；从零开始，并在 Clip 末尾结束。
5. 大角度旋转使用中间偏航 Key。
6. 选择隐式战斗扭曲或显式控制；任意精确保留控制都会关闭隐式模式。
7. 在净运动方向发生改变的阶段之间放置扭曲边界。
8. 记住显式扭曲仍然需要角色拥有当前战斗目标。
9. 如果 Behavior 要读取 `AMR_IsAnimationWarpingEnabled`，请与 Behavior Data Injector 一起安装随附 BDI JSON。
10. 查看 `AnimationMotionRevolution.log` 中的时长不一致、错误控制、分段摘要及倍率诊断。

重要限制：

- AMR 不会扭曲只有原版根运动的数据；运动扭曲需要自定义 `animmotion`。
- 扭曲只缩放，不会旋转、预测目标移动或每帧追踪移动目标重新缩放。
- 距离使用角色 Reference Position，而非碰撞表面间距。
- 过大的角度范围可能允许缩放相对目标横向甚至背离目标的运动。
- 反向路径按段净位移计算，必须有意识地分段。
- 悬崖保护是向下物理支撑检查，不是 Navmesh 寻路。
- 运动/旋转 Key 分散在多个 Track 或同一时间重复时，当前解析器无法可靠定义结果。

原始注释约定请参阅 [Animation Motion Revolution 模组页面](https://www.nexusmods.com/skyrimspecialedition/mods/50258)。图变量注入格式请参阅 [Behavior Data Injector 配置指南](https://github.com/max-su-2019/BehaviorDataInjector/blob/master/doc/How%20to%20create%20BDI%20config%20files.md)。
