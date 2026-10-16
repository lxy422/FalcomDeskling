#pragma once

#include <string>
#include <vector>

namespace fd {

// 一个能上场的人：名字就是她那个夹的名字，路径是绝对路径。
// id 是从她那份 character.xml 里读出来的"这是一类的谁"，同类可以共用一个号 ——
// 那张表还没做，这里先留 0。
struct SkinInfo {
    std::wstring name;
    std::wstring dir;
    bool hasStandby = false;   // false = 武器那两套不齐，托盘里要标出来
    int  id = 0;
};

// ---- 资源住在哪儿 ----
//
// 只认 exe 旁边那一级：chr\ 和 Enemy\ 是两个独立的地方，分开找。借 ChrRoot()
// 去推 Enemy 的路会跑去找 chr\Enemy 那个根本不存在的夹 —— WpfTest 那边
// EnemyStore.vb:28 专门写了这条教训。
//
// 也不再往上翻目录树、更不兜某个开发机上的绝对路径：那种兜底会让"资源找不到"
// 看起来像"这个人缺图"，白查半天。找不到就照实说，并且说清楚看了哪个路径。
const std::wstring& ChrRoot();
const std::wstring& EnemyRoot();

// 不管找没找到，程序打算去哪儿看 —— 报错的时候得靠它说实话
const std::wstring& ChrRootGuess();
const std::wstring& EnemyRootGuess();

// --root= / --enemyroot= 给的显式路径。要在任何一次扫描之前叫，之后叫就晚了
void SetRootOverrides(const std::wstring& chr, const std::wstring& enemy);

// 有 2.Walk 就算这个人存在。判据只有这一处，别的地方不许再写一遍
bool HasWalkSet(const std::wstring& dir);

// 排过序的一遍名单：菜单和"随机定主角"都从这儿拿，顺序定下来才不会
// 每次启动菜单换个样子
std::vector<SkinInfo> ScanChr();

// 敌人索引。**只枚举目录名，一张图都不解** —— 那边真算下来是 207MB、6000 张，
// 启动就全扫一遍等于把冷启动从 0.3 秒拖到 3 秒。齐不齐留到真要点名加的时候再验
std::vector<SkinInfo> ScanEnemyIndex();

// 这个夹是不是挂在 Enemy\ 底下
bool IsEnemyDir(const std::wstring& dir);

}  // namespace fd
