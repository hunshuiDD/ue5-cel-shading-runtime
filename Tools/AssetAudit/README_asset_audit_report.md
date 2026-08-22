# asset_audit_report.py 使用说明

把 UE Asset Audit（资产审计）/ Statistics 面板导出的 CSV，加工成分团队、带条件色标记的 Excel 报表。对应 Unreal Fest 2025《Technical Artist's Guide to Profiling in Unreal Engine》(Crystal Dynamics) 里"资产治理"那一步，方法论详见 `Skills/ue-perf-profiling.skill`。

通用工具，不绑定具体项目——团队分类、阈值、权重都在外部 JSON 配置里，换项目只改配置，不改代码。

## 依赖

```
pip install pandas openpyxl
```

## 第一步：从 UE 里导出 CSV

菜单：工具 → 审计 → 资产审计（Asset Audit）。这个面板不能直接选文件夹，用法是：

1. 在内容浏览器里点开要审计的文件夹，让资产列表显示出该文件夹下的资产。
2. `Ctrl+A` 全选（或框选需要的部分）。
3. 拖到"资产审计"面板的空白列表区域，松手批量加入。
4. 面板右上角"选定平台"建议保持 `Editor`（切到 `Custom` 可能触发"无法获取AssetReg"警告，部分列会算不出来）。
5. 导出为 CSV（不同 UE 版本入口可能在工具栏图标或列表右键菜单里，没有固定统一位置，实际点开找）。

## 第二步：跑脚本

**方式一：拖拽（最省事）**

把导出的 CSV 拖到 `run_asset_audit.bat` 上，报表会生成在 CSV 同目录下，文件名是 `原CSV名_report.xlsx`。这个 bat 使用目录内的示例 config（`asset_audit_config.sample.json`）；按需替换为目标项目配置。

**方式二：命令行**

```
python asset_audit_report.py 输入.csv -o 报表.xlsx --config asset_audit_config.sample.json
```

- `input`（必填，位置参数）：Asset Audit 导出的 CSV 路径。
- `-o/--output`：输出 xlsx 路径，不填默认 `asset_report.xlsx`。
- `--config`：项目配置 JSON 路径。不传则用脚本内置的通用默认值；使用目录内的 `asset_audit_config.sample.json` 可启用示例 Character 分类规则。

## 输出说明

xlsx 里每个 sheet：

- **Overview**：全部资产按 CostScore 降序排列。
- **按团队分表**（Character/Environment/VFX/UI/…，取决于 config 里 `teams` 定义）：资产路径或名字命中对应关键词的子集。
- **Config**：这次跑报表用的权重公式、各阈值数值，方便甩给别人时对方知道怎么算出来的。

派生列含义：

| 列 | 含义 |
|---|---|
| TotalTris | TriCount × Instances，总面数（比单体面数更能反映真实开销） |
| TriDensity | TriCount / 包围盒对角线长度，面数密度异常的小资产更容易被看出来（需要 CSV 里有 Dimensions 列才会算） |
| CostScore | 0-100 综合成本分，按 config 里 `cost_weights` 加权后 min-max 归一化，是启发式排序分不是绝对值 |
| Severity | OK/Warn/Crit，任一指标超过 config 里 `thresholds` 的 crit 门槛记 Crit，超 warn 记 Warn |

## 换项目怎么改

1. 复制一份 `asset_audit_config.sample.json`，改名（如 `asset_audit_config.newproject.json`）。
2. 编辑里面的 `teams`（角色名/路径关键词换成新项目的）、`thresholds`（面数/材质数/磁盘体积门槛按新项目资产规模重新定，不要照抄）。`cost_weights`/`column_hints` 一般不用动。
3. 复制一份 `run_asset_audit.bat`，把里面 `asset_audit_config.sample.json` 那一行换成新配置文件名。
4. 不确定新项目阈值该怎么定时，可以先不传 `--config` 跑一次通用默认值，看结果再调数字。

## 已知限制 / 踩坑

- **列名靠关键词模糊匹配自动探测**，UE 版本/语言不同，列名可能对不上。本项目实测中，`资产审计` 导出的 CSV 里 `HasPerInstanceRandom`（布尔字段）曾被误判成 Instances 列、`MaterialDecalResponse`（枚举字段）被误判成 MaterialCount 列——这两个维度算出来的数值不准，不影响脚本运行，但报表上这两列不能直接信。真要用准确的实例数/材质数，需要去查 CSV 表头里真正对应的列名，写进 config 的 `column_hints` 里手动指定。
- **TriDensity 依赖 Dimensions 列**，CSV 里没有这一列时该列全 NaN，不报错。
- **`--config` 缺失字段会用整块默认值补齐**（不是逐项合并），改一个阈值也要把 `thresholds` 整块写全。
- 本机跑通过一次真实数据；`run_asset_audit.bat` 未做除拖拽单文件外的批量场景验证。

## 相关文件

- `asset_audit_report.py` — 脚本本体
- `asset_audit_config.sample.json` — 示例配置（Character/Environment 等团队分类、阈值）
- `run_asset_audit.bat` — 本机一键运行封装
