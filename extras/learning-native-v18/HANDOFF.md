# 当前检查点：15 项通过，补齐真实 Holder 实现

主窗口报告原15/15 MSVC单元已通过，原生链接参数检查通过；从实际game.lib提取的SpellEffects对象CodeView校验值与当前源文件匹配。尚未LINK或开服。实际modules.lib不含新Holder管理接口，故只换头文件不够。

本轮增加真实PlayerbotMgr.cpp为第16个单元，原15项全部复用，命令见native_candidate/PARENT_LOCAL_ACTIONS.md。原生CharacterLoginPending头/核心接线、现用Holder源/库成员来源仍需主窗口核对，不能用伪造跟踪器或桩函数补位。链接入口随之支持96显式/102总对象，仍无学习策略控制或真实训练episode。

源码分支dev/learning-shadow-v18，目录extras/learning-native-v18/native_candidate。以下旧检查点按历史保留；以本节和当前续作文件为准。

# 可学习队伍 AI — 接手入口

## 当前准确状态

本目录 `/workspace/shared/playerbot-learning-lab` 是云端离线实验室，旁边的 `../raid-learning-interface` 是 Python 执行边界原型。**没有部署到魔兽；没有接管团本主体开发；没有真人/真实副本训练记录。** 当前线性模型只用合成数据训练，语义漂移压力测试失败。保持影子模式，不得把它放进正式服，不要宣称会带队或比原 AI 更聪明。

用户目标：随机本安静跟队与补位；团本/挑战本可选指挥，能救玩家失误、安排打断/换坦/减伤。不能偷偷开怪、无敌、传送跳机制或控制真人角色。优先云端，别打扰用户电脑和正式服。可以借鉴现成项目，但复用与真实验证必须说清楚。

## 本轮实际完成

- 独立审计后补了时钟跨调用倒退、字符串 false 等异常授权输入、模型准入坏证据防御。C++ 移动租约也补了同类时钟检查。
- 回放升为明确的 v2：全 run 划分训练与测试；绑定构建/来源/实例/队伍/会话；多角色独立时间流；epoch递增；每次决定只允许一条终态，保留失败而不重复计权；有字节、行数及episode预算。
- 特征不再接受 bool/字符串；评分输入限定候选数量。
- 新增 CPU-only 评分器资源短测和数据就绪清单。没有偷偷重训以掩盖已有压力测试失败。

## 快速阅读与命令

先读 README、SOURCE_ALIGNMENT、VALIDITY_REVIEW、TELEMETRY_V2、CLOUD_TRAINING_PLAN、HYBRID_MODEL_OPTION。

```
PYTHONDONTWRITEBYTECODE=1 python -m unittest -q
PYTHONDONTWRITEBYTECODE=1 python replay.py artifacts/replay_fixture.jsonl artifacts/model.json
PYTHONDONTWRITEBYTECODE=1 python resource_evaluation.py --output artifacts/resource_report.json
```

Python实验室73项、旁边执行边界20项通过。C++源码最新release测试41项断言通过，10万次选择校验通过；旧 `cpp/test_shared` 二进制没有重编覆盖，不能把旧二进制当最新源码证据。最新测试输出在 artifacts/revision_tests.txt、artifacts/envelope_revision_tests.txt、artifacts/cpp_revision.txt。C++仍然链接的是test stubs，没有真实worldserver。

## 资源证据的限制

见 artifacts/resource_report.json：同一云端CPU短测，25角色各5候选中位约0.48ms；500角色各128候选约237ms。它只反复评分同一组合成快照，RSS约13MiB是整个Python进程峰值，包含解释器；不是500个真实AI、全服tick、寻路、数据库或训练占用。不能据此保证用户PC不卡。应按事件/目标改变触发评分并分批限额，不要每帧给500名AI做128候选全量评分。

## 下一步必须拿到的东西

1. 与实际正式F509及团本分支匹配的完整源码/构建身份。已有14文件片段校验通过，但不是完整工程。
2. 由原团本/服务端开发者评审并加入**影子观察器**，导出真实候选、原生合法性、上下文和终态。参见 TELEMETRY_V2；现有日志不够，不能造字段。
3. 少量完整真实副本run：入口/水/坡/门、休息和吃喝被打断、掉线重聚、灭团与phase切换。用户无需专门陪练，但不能凭合成数据声称真实跑通。
4. 先比较原生A和共用协调B，再测试模型C。评估整个run的成功/救场/绕圈/误开怪和CPU延迟；不用分数或动作一致率冒充实战成效。

目前完整云端服务器、世界库和地图/导航数据未配置；也没有大模型API或付费服务。需要接入时由主体服务端开发任务对接，不能直接碰用户游戏。工作到此有可保存检查点，后续从真实数据接入继续，不重复跑一堆合成训练。

## 后续检查点：原生日志、玩家错误与上游复用

- 用户允许必要的独立后台服务，但不占桌面前端；这轮没有启动任何服务。当前云端没有完整worldserver/world/nav环境，只有已核验源码切片。
- 对工作区和资料库进行了有范围的运行轨迹查询，未找到可核实的真实副本日志。检索没有命中不证明别处不存在；部署回执仍不是运行证据。
- 新增 `native_diagnostics.py`/`NATIVE_DIAGNOSTICS.md`，严格解析F509旧DMOVE请求日志，保留偏移与带密钥摘要，处理错误/重复/预算，检查Detector不变量，原子写报告。原GUID、动作和日志前缀不导出；所有就绪标记false，永不伪造v2训练行。人工语法夹具和报告在artifacts/native_diagnostic_fixture*。
- 新增 `fault_scenarios.py`，固定seed生成十类隔离错误/干扰计划，包括站火、抢开、多拉、漏职责、矛盾/多人指令和连续错误/不可救。未来事件不放入模型当前观察；计划无结果/奖励，不等于已经训练。样本为artifacts/fault_scenario_plan.json。
- 新增 `NATIVE_BASELINE_PLAN.md`，逐点写明Engine/F509无副作用插桩、前置/重试ID、同构建A原生/B协作/C影子的对照。特别是listener可以不调用Execute仍返回OK，不能靠OK断言实际派发。C影子没有控制权，不能证明胜率提升。
- 新增 `UPSTREAM_REUSE.md`：优先LoA同版本、mod-playerbots协作与避害、CMaNGOS TrackLoop；训练方法参考Human-AI Robustness，真实隔离接口优先PettingZoo。当前只读核查源码/许可，没有复制/安装上游代码，也没有找到现成5.4.8带团模型。
- 本轮新模块有24项针对性测试（12导入+12故障计划）；原有73项实验室、20项执行边界、41项C++stub结果保留此前检查点，不为了记总数重复跑所有旧检查。新输出见artifacts/native_followup_tests.txt。

下一步仍需由主体任务提供完整匹配构建/独立环境或已有真实轨迹，在源码所有者评审后接原生观察器；不要求用户陪着打本，不误接正式数据库。模型语义漂移失败仍未消失，保持shadow-only，不能上线。

## 已运行的闭环检查点与最新用户方向

- offline_loop.py/synthetic_party_env.py 已实际完成首轮有状态toy收集→拟合→持出对照→失准压力→不可变记录闭环，22项新增针对性检查通过。912 toy runs，5760训练/19767评估决定；输出约2MiB，详见CLOSED_LOOP_STATUS.md和artifacts/closed-loop-v1/receipt.json。真实run仍0，未训练真实职业/团本，没有部署。
- 学习和安全规则的toy完成率相同，未证明胜过成熟规则；danger校准失准时奖励下降。不要用随机弱基线或完成率掩盖退化。旧压力失败/旧模型保留哈希验证未被改写。
- UPSTREAM_LOCKS.json固定六项目真实commit/许可与源码摘要；均未安装/集成。CMaNGOS独立模块许可证未解决，只参考思想不复制代码。
- OLD_MOP_SOURCE_FACTS.md调查原版5.4.8/18414的12类技能配合；MOP_KNOWLEDGE_BOUNDARY.md和artifacts/old_mop_knowledge_audit.json明确上游/本地未核验层级、错动作名/错注释/内部效果ID、运行态条件。没用MoP Classic数值，尚缺用户DBC/SQL/完整构建。
- 用户明确允许现有正式服里专用测试角色+单独实例，不要把复制整服/新数据库当强制前提；其后又要求先写好模型。当前没有到用户Windows/服务端的执行连接，未创建角色或连服。
- 用户提出后台AI队可自己跑本/战场/团本，训练时不用和真人见面；玩家离线仍可练。BACKGROUND_TRAINING_INTEGRATION.md记录调度/异步训练/版本管理方案，未实施常驻服务。
- 用户要求融入已有团本AI进度，保留机制/职责/难度代码与F509补丁。当前只有V14等少量切片，不能宣称已合入完整团本主体；从主体任务拿匹配当前分支后接观察器与职责接口，别重写或覆盖已有工作。

## 本轮原生候选与实际DBC检查点

- 已materialize并按archiveSHA校验最新15,392,177B V15包，2864成员hash全通过。云端解压80,756,475B，仅一份工作树，不是完整最新合并工程。最新输入状态见native_candidate/native_input_status.json。
- 真实Engine/Spell/Unit覆盖层已定点审阅，形成engine-shadow-v17、interrupt-observer-v17、bot-only-mode-v17及后台queue。代码默认关闭，未启动游戏服、未部署模型，未宣称完整native构建。
- 原生输入审计：22/23header和81/83 source-object关联记录的源字节实际匹配；obj/EXE/库未转移。SpellEffects仅上传base，未证实当前链接成员相同；不要盲贴。
- 实际33ID机制核查已完成20表join。旧“云端无DBC/只有14切片”的绝对状态不再成立；SQL绑定、最终SpellInfo、真实角色能力仍未验证。
- 用户新增明确要求取消真人陪同资格；Astra最高审查及候选实现已执行，仅删除该前提，保留技术正确性检查，未部署。
- 用户最后指定训练三队：5/10/25，共40，共享经验库，人数/难度分层；训练也可反馈疑似副本BUG。不能继续采用早期单队/在线2队10人草案。最新队列默认和验证应看queue-verification.json。
- 已收到此前排队的三条消息（含“一支太少了吧”截图），已逐条答复并纳入三队/经验/BUG方案。没有把主窗口其它游戏请求当成本线程新任务。

- 最终queue由root按最新用户定案接管，header SHA8689a05dc1e4f30a8053e619778a0ae5e3aafeb09509b5aaf5205f6848065d11，191 release断言通过。之前121/148报告是旧草案，最新以queue-verification为准。
- 用户新增授权：原生编译/部署后先开服，用户本人暂不上线，便于控制。当前未启动服务器或部署模型。主窗口拥有本地环境，可在完成实际构建/接线后按授权执行，不必再问一遍。

## V17候选已实际交付资料库

交付元数据见artifacts/native_v17_library_delivery.json，Library创建成功且本地身份已用官方helper写入。源码包563871B，SHA0119f32b891de661402fd2c115ae54e973976a98c3482a64aeba5f8e3eaf83b2，69个文本/源成员；未含EXE/OBJ/整DBCs/旧toy训练轨迹。消费者按metadata引用materialize并核哈希，不重复上传。主窗口已有本地完整构建连接，下一步按native_candidate/BUILD_NEXT.md增量核对编译；SpellEffects当前性、provider/真实binding/退休/队列controller仍需接线，不可称只差部署。用户已授权编译部署后先开服、本人不上线；模型继续影子，不能放开当前合成模型控制。
