#include "support/i18n.hpp"

#include <iterator>
#include <unordered_map>

namespace lnb::i18n {

namespace {

// One translation entry: the English source text (the lookup key) and its
// Traditional Chinese and Japanese renderings. Entries are grouped by the
// window or page they belong to; within a group they follow the source order,
// which keeps the table easy to diff against the code.
struct Entry {
  std::string_view en;
  const char* zh; // Traditional Chinese, UTF-8
  const char* ja; // UTF-8
};

constexpr Entry kTranslations[] = {
    // General.
    {"Close", "關閉", "閉じる"},
    {"Save", "儲存", "保存"},
    {"Cancel", "取消", "キャンセル"},
    {"Create", "建立", "作成"},
    {"Edit", "編輯", "編集"},
    {"Copy", "複製", "コピー"},
    {"Select all", "全選", "すべて選択"},
    {"Open", "開啟", "開く"},
    {"Run", "執行", "実行"},
    {"Settings", "設定", "設定"},
    {"Configuration", "配置", "設定"},
    {"Metrics", "度量", "メトリクス"},
    {"Assistant", "助手", "アシスタント"},
    {"Account", "帳戶", "アカウント"},
    {"About", "關於", "このアプリについて"},
    {"Language", "語言", "言語"},
    {"Language changed", "語言已更改", "言語を変更しました"},
    {"The interface language will change after Local Notebook restarts. Restart now?", "介面語言將在 Local Notebook 重啟後更改。現在重啟嗎？", "インターフェースの言語は Local Notebook の再起動後に変更されます。今すぐ再起動しますか？"},
    // Error messages shown in the transcript.
    {"The request failed for an unknown reason.", "請求因未知原因失敗。", "不明な理由でリクエストが失敗しました。"},
    // Configuration page.
    {"Appearance", "外觀", "外観"},
    {"Theme", "主題", "テーマ"},
    {"Interface font", "介面字型", "インターフェースフォント"},
    {"Interface size", "介面字號", "インターフェースのサイズ"},
    {"Chat font", "聊天字型", "チャットフォント"},
    {"Chat size", "聊天字號", "チャットのサイズ"},
    {"Monospace font", "等寬字型", "等幅フォント"},
    {"Monospace size", "等寬字號", "等幅フォントのサイズ"},
    {"Mono size", "等寬比例", "等幅比率"},
    {"Mono size as a fraction of the surrounding text.", "等寬文字字號相對於周圍文字的比例，作用於內聯程式碼與程式碼塊。", "等幅テキストのサイズを周囲のテキストに対する比率で指定します。インラインコードとコードブロックの両方に適用されます。"},
    {"Tool strip size", "工具條字號", "ツール行のサイズ"},
    {"Inline summary", "內聯摘要", "インライン要約"},
    {"Compacted into #%1", "已壓縮至 #%1", "#%1 に圧縮済み"},
    {"Steered", "已插話", "割り込み済み"},
    {"Steer the running turn", "插入當前回覆", "進行中の応答に割り込む"},
    {"Tool call and result size as a fraction of the message text.", "工具呼叫與結果字號相對於訊息文字的比例。", "ツール呼び出しと結果のサイズをメッセージ本文に対する比率で指定します。"},
    {"Composer", "輸入框", "入力欄"},
    {"Send with Ctrl+Enter", "使用 Ctrl+Enter 傳送", "Ctrl+Enter で送信"},
    {"When on, Ctrl+Enter sends and Enter inserts a new line.", "開啟時，Ctrl+Enter 傳送，Enter 換行。", "オンのとき、Ctrl+Enter で送信し、Enter で改行します。"},
    {"Transcript", "對話記錄", "トランスクリプト"},
    {"Rendered messages", "渲染的訊息數", "表示するメッセージ数"},
    {"History page size", "歷史分頁大小", "履歴のページサイズ"},
    {"Startup", "啟動", "起動"},
    {"Open the current assistant automatically on next launch", "下次啟動時自動開啟當前助手", "次回起動時に現在のアシスタントを自動で開く"},
    {"When off, the assistant picker is shown at every launch.", "關閉時，每次啟動都會顯示助手選擇器。", "オフのとき、起動のたびにアシスタント選択画面を表示します。"},
    {"Background", "背景", "背景"},
    {"Image", "圖片", "画像"},
    {"Path to a background image (optional)", "背景圖片路徑（可選）", "背景画像のパス（任意）"},
    {"Tint the image", "為圖片著色", "画像に着色する"},
    {"Restore to defaults", "恢復預設值", "デフォルトに戻す"},
    {"Reset this page's fields to their shipped defaults. Nothing is saved until you press Save.", "將此頁面的欄位重置為出廠預設值。在你按下“儲存”之前不會儲存任何內容。", "このページの項目を出荷時のデフォルトに戻します。「保存」を押すまで何も保存されません。"},
    {"Application-wide options stored in data/settings.json.", "儲存在 data/settings.json 中的應用級選項。", "data/settings.json に保存されるアプリ全体のオプション。"},
    // Metrics page.
    {"Layout", "佈局", "レイアウト"},
    {"Sidebar width", "側邊欄寬度", "サイドバー幅"},
    {"Task list width", "任務列表寬度", "タスクリストの幅"},
    {"Page margin", "頁面邊距", "ページ余白"},
    {"Message spacing", "訊息間距", "メッセージ間隔"},
    {"Composer height", "輸入框高度", "入力欄の高さ"},
    {"Expanded height", "展開高度", "展開時の高さ"},
    {"Session item height", "會話項高度", "セッション項目の高さ"},
    {"Title font size", "標題字號", "タイトル文字サイズ"},
    {"Flow max width", "正文最大寬度", "本文の最大幅"},
    {"Bubble radius", "氣泡圓角", "吹き出しの角丸"},
    {"Reading rhythm", "閱讀節奏", "リーディングリズム"},
    {"Line heights are percentages (100 is single spacing).", "行高為百分比（100 為單倍行距）。", "行の高さはパーセントです（100 が単一倍）。"},
    {"Assistant line height", "助手行高", "アシスタントの行の高さ"},
    {"User line height", "使用者行高", "ユーザーの行の高さ"},
    {"Paragraph spacing", "段間距", "段落間隔"},
    {"List spacing", "列表間距", "リスト間隔"},
    {"Reset these metrics to the current theme's own defaults. Nothing is saved until you press Save.", "將這些度量重置為當前主題自身的預設值。在你按下“儲存”之前不會儲存任何內容。", "これらのメトリクスを現在のテーマ自身のデフォルトに戻します。「保存」を押すまで何も保存されません。"},
    {"Sizing and spacing of the interface.", "介面的尺寸與間距。", "インターフェースのサイズと間隔。"},
    // Assistant page.
    {"Assistant memory", "助手記憶", "アシスタントメモリ"},
    {"Memory updated.", "記憶已更新。", "メモリを更新しました。"},
    {"Recent messages pool", "最近訊息池", "最近のメッセージプール"},
    {"Update memory now", "立即更新記憶", "今すぐメモリを更新"},
    {"Updating memory...", "正在更新記憶...", "メモリを更新中..."},
    {"Memory Update Failed", "記憶更新失敗", "メモリの更新に失敗しました"},
    {"There is no new conversation to merge into memory yet.", "還沒有可合併到記憶中的新對話。", "メモリに統合する新しい会話はまだありません。"},
    {"Update summarization now", "立即更新摘要", "今すぐ要約を更新"},
    {"Merge the recent messages of every session into the memory below, ignoring the automatic memory trigger. Idempotent: pressing it again with no new messages changes nothing.", "忽略自動記憶觸發閾值，將所有會話的最近訊息合併到下面的記憶中。冪等：沒有新訊息時再次按下不會改變任何內容。", "自動メモリトリガーを無視して、すべてのセッションの最近のメッセージを下のメモリに統合します。冪等：新しいメッセージがない状態で再度押しても何も変わりません。"},
    {"Automatically maintained record shared across every session. Edit to correct it, or clear it to rebuild it from the transcript.", "在所有會話間共享的自動維護記錄。可以編輯以修正，或清空以從對話記錄重新構建。", "すべてのセッションで共有される自動維持の記録です。編集して修正するか、消去してトランスクリプトから再構築してください。"},
    {"<b>How assistant memory works</b><br>This memory is shared across every session of the assistant, and it is only rewritten when you ask. <b>Update memory now</b> merges it with each session's newest summary (bounded by <i>Session pool</i>) and with the newest messages across all sessions (bounded by <i>Recent messages pool</i>). The result is capped by <i>Memory budget</i>. Older conversation may therefore be forgotten. Running it again with no new conversation changes nothing. Custom instructions are kept separate and are never touched.", "<b>助手記憶的工作原理</b><br>此記憶在該助手的每個會話之間共享，且只在你要求時才會重寫。<b>立即更新記憶</b> 會將它和每個會話最新的摘要（受 <i>會話池</i> 限制）以及所有會話中最新的訊息（受 <i>最近訊息池</i> 限制）合併。合併結果受 <i>記憶預算</i> 限制。因此較早的對話可能會被遺忘。沒有新對話時再次執行不會改變任何內容。自定義指令單獨儲存，永遠不會被觸碰。", "<b>アシスタントメモリの仕組み</b><br>このメモリはアシスタントのすべてのセッションで共有され、あなたが求めるときだけ書き換えられます。<b>今すぐメモリを更新</b> は、各セッションの最新の要約（<i>セッションプール</i>で上限）と、すべてのセッションの最新メッセージ（<i>最近のメッセージプール</i>で上限）を統合します。結果は<i>メモリバジェット</i>で上限が設定されます。そのため古い会話は忘れられる可能性があります。新しい会話がない状態で再度実行しても何も変わりません。カスタム指示は別に保持され、決して変更されません。"},
    {"Updating summarization...", "正在更新摘要...", "要約を更新中..."},
    {"Summarization Update Failed", "摘要更新失敗", "要約の更新に失敗しました"},
    {"Nothing to Update", "無可更新內容", "更新するものがありません"},
    {"There is no new conversation to summarize yet.", "還沒有可摘要的新對話。", "要約する新しい会話はまだありません。"},
    {"The assistant open in this window. It is fixed for this run, but its configuration and memory can be edited here.", "此視窗中開啟的助手。它在本次執行期間固定不變，但其配置和記憶可以在此編輯。", "このウィンドウで開いているアシスタント。この実行中は固定ですが、構成とメモリはここで編集できます。"},
    // Account page.
    {"Current session", "當前會話", "現在のセッション"},
    {"Startup", "啟動", "起動"},
    {"Open at startup", "啟動時開啟", "起動時に開く"},
    {"Open this assistant automatically at startup. When off, the assistant picker is shown every time.", "啟動時自動開啟此助手。關閉時，每次都會顯示助手選擇器。", "起動時にこのアシスタントを自動的に開きます。オフのときは毎回アシスタント選択画面が表示されます。"},
    {"No session selected", "未選擇會話", "セッションが選択されていません"},
    {"Session #%1", "會話 #%1", "セッション #%1"},
    {"The session you are in, and how the app starts.", "你所在的會話，以及應用的啟動方式。", "現在のセッションと、アプリの起動方法。"},
    // About page.
    {"A local, assistant-backed notebook.", "一個本地、由助手驅動的筆記本。", "ローカルでアシスタントを備えたノートブック。"},
    {"Version %1", "版本 %1", "バージョン %1"},
    {"Built with Qt %1", "使用 Qt %1 構建", "Qt %1 でビルド"},
    {"License", "許可", "ライセンス"},
    {"Distributed under the terms in LICENSE.txt.", "依據 LICENSE.txt 中的條款分發。", "LICENSE.txt の条項に基づいて配布されています。"},
    {"Program information.", "程式資訊。", "プログラム情報。"},
    // Main window.
    {"Toggle sidebar", "切換側邊欄", "サイドバーの表示切替"},
    {"New Session", "新建會話", "新しいセッション"},
    {"PINNED", "已固定", "ピン留め"},
    {"SESSIONS", "會話", "セッション"},
    {"Select a session, or create one to start.", "選擇一個會話，或新建一個以開始。", "セッションを選択するか、新規作成して開始してください。"},
    {"No tasks running", "沒有正在執行的任務", "実行中のタスクはありません"},
    {"1 task running", "1 個任務正在執行", "1 件のタスクを実行中"},
    {"%1 tasks running", "%1 個任務正在執行", "%1 件のタスクを実行中"},
    {"Running", "執行中", "実行中"},
    {"Completed", "已完成", "完了"},
    {"Failed", "失敗", "失敗"},
    {"Cancelled", "已取消", "キャンセル済み"},
    {"Delete Session", "刪除會話", "セッションの削除"},
    {"Are you sure you want to delete this session? This cannot be undone.", "確定要刪除此會話嗎？此操作無法撤銷。", "このセッションを削除してもよろしいですか？この操作は取り消せません。"},
    {"summarization trigger: ", "摘要觸發閾值：", "要約トリガー: "},
    {"%1 / %2 conversation tokens (system prompt excluded; the summarization update fires at the limit)", "%1 / %2 會話 token（不含系統提示詞；達到上限時執行摘要更新）", "%1 / %2 会話トークン（システムプロンプトを除く。上限に達すると要約の更新が実行されます）"},
    // Conversation view.
    {"%1 tok", "%1 詞元", "%1 トークン"},
    {"%1k tok", "%1k 詞元", "%1k トークン"},
    {"Type a message...  (Ctrl+Enter to send, Enter for a new line)", "輸入訊息...  （Ctrl+Enter 傳送，Enter 換行）", "メッセージを入力...  （Ctrl+Enter で送信、Enter で改行）"},
    {"Type a message...  (Enter to send, Shift+Enter for a new line)", "輸入訊息...  （Enter 傳送，Shift+Enter 換行）", "メッセージを入力...  （Enter で送信、Shift+Enter で改行）"},
    {"Copy message", "複製訊息", "メッセージをコピー"},
    {"Edit message", "編輯訊息", "メッセージを編集"},
    {"Delete message", "刪除訊息", "メッセージを削除"},
    {"Branch conversation", "分支對話", "会話を分岐"},
    {"Delete Message", "刪除訊息", "メッセージの削除"},
    {"Are you sure you want to delete this message?", "確定要刪除此訊息嗎？", "このメッセージを削除してもよろしいですか？"},
    {"Delete This Message removes only it. Delete This and Following removes it and every message after it. This cannot be undone.", "“僅刪除此訊息”只刪除這一條；“刪除此訊息及之後的訊息”會刪除它及其之後的所有訊息。此操作無法撤銷。", "「このメッセージのみ削除」はこのメッセージだけを削除します。「このメッセージ以降を削除」はこれ以降のすべてのメッセージも削除します。この操作は取り消せません。"},
    {"Delete This Message", "僅刪除此訊息", "このメッセージのみ削除"},
    {"Delete This and Following", "刪除此訊息及之後的訊息", "このメッセージ以降を削除"},
    {"verbatim: %1/%2", "逐字：%1/%2", "逐語: %1/%2"},
    {"inactive", "非活動", "非アクティブ"},
    {"Message:", "訊息：", "メッセージ："},
    {"Update summarization from the conversation", "從對話更新摘要", "会話から要約を更新"},
    {"Send", "傳送", "送信"},
    {"Shrink the composer", "縮小輸入框", "入力欄を縮小"},
    {"Expand the composer", "展開輸入框", "入力欄を展開"},
    // Assistant picker.
    {"Assistants", "助手", "アシスタント"},
    {"Choose an assistant to open, or create a new one.", "選擇要開啟的助手，或新建一個。", "開くアシスタントを選択するか、新規作成します。"},
    {"New assistant", "新建助手", "新規アシスタント"},
    {"Open automatically at startup", "啟動時自動開啟", "起動時に自動的に開く"},
    {"Untitled assistant", "未命名助手", "無題のアシスタント"},
    {"No assistant selected", "未選擇助手", "アシスタントが選択されていません"},
    {"There are no assistants yet. Create one to get started.", "還沒有助手。新建一個即可開始。", "アシスタントがまだありません。作成して始めましょう。"},
    {"Select an assistant on the left to see it here.", "在左側選擇一個助手以在此檢視。", "左側のアシスタントを選択するとここに表示されます。"},
    {"Edit assistant", "編輯助手", "アシスタントを編集"},
    // Session editor.
    {"Edit Session", "編輯會話", "セッションを編集"},
    {"General", "通用", "一般"},
    {"Summarization", "摘要", "要約"},
    {"Memory preset", "記憶預設", "メモリプリセット"},
    {"Summarization triggers", "摘要觸發", "要約トリガー"},
    {"Auto compact reasoning", "摘要推理", "要約の推論"},
    {"The reasoning level used when this session's summarization is updated. Chat turns are unaffected.", "更新此會話摘要時使用的推理級別。聊天回合不受影響。", "このセッションの要約が更新されるときに使用される推論レベル。チャットのターンには影響しません。"},
    {"Session", "會話", "セッション"},
    {"Session title", "會話標題", "セッションのタイトル"},
    {"Directories the assistant may access, one per line (optional)", "助手可訪問的目錄，每行一個（可選）", "アシスタントがアクセスできるディレクトリ（1行に1つ、任意）"},
    {"Title", "標題", "タイトル"},
    {"Reasoning", "推理", "推論"},
    {"The reasoning level this session requests for chat turns.", "此會話為聊天回合請求的推理級別。", "このセッションがチャットのターンに要求する推論レベル。"},
    {"Shared directories", "共享目錄", "共有ディレクトリ"},
    {"One directory per line. The model only sees relative paths inside these directories, and its writes stay inside them. If several directories share a name, the later ones gain a numeric suffix.", "每行一個目錄。模型只能看到這些目錄內的相對路徑，其寫入也限於其中。若多個目錄同名，靠後的目錄會獲得數字字尾。", "1行に1つのディレクトリ。モデルはこれらのディレクトリ内の相対パスのみを認識し、書き込みもその中に限定されます。複数のディレクトリが同じ名前を持つ場合、後のものには数値の接尾辞が付きます。"},
    {"Tool permissions", "工具許可權", "ツールの権限"},
    {"Enable for this session", "為此會話啟用", "このセッションで有効にする"},
    {"When off, no tools are offered to the model for this session. The switches below keep their values.", "關閉時，不會向該會話的模型提供任何工具。下方的開關會保留其取值。", "オフのとき、このセッションのモデルにはツールが提供されません。下のスイッチは値を保持します。"},
    {"Read files", "讀取檔案", "ファイルを読み取る"},
    {"Allow listing, searching and reading files in the shared directories.", "允許在共享目錄中列出、搜尋和讀取檔案。", "共有ディレクトリ内のファイルの一覧表示、検索、読み取りを許可します。"},
    {"Write files", "寫入檔案", "ファイルに書き込む"},
    {"Allow creating, overwriting, editing and deleting files in the shared directories.", "允許在共享目錄中建立、覆蓋、編輯和刪除檔案。", "共有ディレクトリ内のファイルの作成、上書き、編集、削除を許可します。"},
    {"Search online", "線上搜尋", "オンライン検索"},
    {"Allow web searches and fetching web pages.", "允許搜尋網路並獲取網頁。", "ウェブ検索とページの取得を許可します。"},
    {"Search endpoint", "搜尋端點", "検索エンドポイント"},
    {"Base URL of a SearXNG-compatible search service, e.g. the local browser bridge at http://127.0.0.1:8873. Leave empty to disable web search.", "相容 SearXNG 的搜尋服務的基礎 URL，例如位於 http://127.0.0.1:8873 的本地瀏覽器橋接。留空則禁用網路搜尋。", "SearXNG 互換の検索サービスのベース URL（例: http://127.0.0.1:8873 のローカルブラウザブリッジ）。空欄にするとウェブ検索を無効にします。"},
    {"Tools", "工具", "ツール"},
    {"Read", "讀取", "読み取り"},
    {"Write", "寫入", "書き込み"},
    {"Online", "線上", "オンライン"},
    {"Custom instructions", "自定義指令", "カスタム指示"},
    {"Kept separate from the generated summary; never overwritten by it.", "與生成的摘要分開儲存；永遠不會被其覆蓋。", "生成された要約とは別に保持され、上書きされることはありません。"},
    {"Merge the conversation into the summary below, ignoring the automatic summarization trigger. Idempotent: pressing it again with no new messages changes nothing.", "忽略自動摘要觸發閾值，將對話合併到下面的摘要中。冪等：沒有新訊息時再次按下不會改變任何內容。", "自動要約トリガーを無視して、会話を下の要約に統合します。冪等：新しいメッセージがない状態で再度押しても何も変わりません。"},
    {"Include in assistant memory", "包含在助手記憶中", "アシスタントメモリに含める"},
    {"When off, this session's messages and summarization are left out of the shared assistant memory. This session's own summarization is unaffected.", "關閉時，此會話的訊息和摘要不會進入共享的助手記憶。此會話自身的摘要不受影響。", "オフのとき、このセッションのメッセージと要約は共有アシスタントメモリに含まれません。このセッション自身の要約は影響を受けません。"},
    {"Automatically maintained record of older turns. Edit to correct it, or clear it to rebuild it from the whole transcript.", "自動維護的較早輪次記錄。可編輯以修正，或清空以從完整對話記錄重新構建。", "古いやり取りの自動維持される記録です。編集して修正するか、消去してトランスクリプト全体から再構築してください。"},
    {"Conversation summary position: the highest message id already merged into the summary above. Used only to decide when the automatic summarization update fires.", "對話摘要位置：已合併到上方摘要中的最大訊息 ID。僅用於判斷自動摘要更新何時發生。", "会話要約位置：上の要約に既に統合された最大メッセージ ID。自動要約更新の発火時期を判断するためにのみ使用されます。"},
    {"Assistant memory position for this session: the highest message id already merged into the shared assistant memory.", "此會話的助手記憶位置：已合併到共享助手記憶中的最大訊息 ID。", "このセッションのアシスタントメモリ位置：共有アシスタントメモリに既に統合された最大メッセージ ID。"},
    {"Summarization watermark", "摘要水位", "要約ウォーターマーク"},
    {"Assistant watermark", "助手水位", "アシスタントウォーターマーク"},
    {"Stored summary versions, newest first. Select one to view its summary and watermark; right-click one to remove it.", "已儲存的摘要版本，最新的在前。選擇一個以檢視其摘要和水位；右鍵單擊可移除。", "保存された要約バージョン（新しい順）。選択すると要約とウォーターマークが表示されます。右クリックで削除できます。"},
    {"Message %1", "訊息 %1", "メッセージ %1"},
    {"Active", "當前", "有効"},
    {"Remove summary version", "移除摘要版本", "要約バージョンを削除"},
    {"Merged up to message %1, made at %2.", "已合併至訊息 %1，建立於 %2。", "メッセージ %1 まで統合、作成日時 %2。"},
    {"Budgets", "預算", "予算"},
    {"Tool limits", "工具限制", "ツールの制限"},
    {"Apply a preset...", "應用預設...", "プリセットを適用..."},
    {"Preset", "預設", "プリセット"},
    {"Fill the summarization fields below with a preset. It also sets the response budget on the General page, which must not exceed the summarization trigger. Nothing is saved until you press Save.", "使用預設填充下面的摘要欄位。它還會設定“通用”頁面中的回覆預算，該預算不得大於摘要觸發閾值。在你按下“儲存”之前不會儲存任何內容。", "プリセットで下の要約項目を入力します。「一般」ページの応答バジェットも設定されます（要約トリガーを超えてはいけません）。「保存」を押すまで何も保存されません。"},
    {"Fill the tool limits below with a preset. Nothing is saved until you press Save.", "使用預設填充下面的工具限制。在你按下“儲存”之前不會儲存任何內容。", "プリセットで下のツール制限を入力します。「保存」を押すまで何も保存されません。"},
    {"Summarization trigger", "摘要觸發閾值", "要約トリガー"},
    {"Summarization content", "摘要內容", "要約内容"},
    {"Memory budget", "記憶預算", "メモリバジェット"},
    {"Summarization budget", "摘要預算", "要約バジェット"},
    {"Response budget", "回覆預算", "応答バジェット"},
    {"Input token produced midway", "中途產生的輸入 token 數", "途中で生成される入力トークン数"},
    {"Total tokens of tool output this session may feed back to the model in one turn, counted across every tool call in that turn. Once the budget is spent, the remaining answer is produced without tools. 0 disables the limit.", "此會話在一輪對話中最多可回傳給模型的工具輸出 token 總數，按該輪所有工具呼叫累計。預算用盡後，剩餘的回覆將在不啟用工具的情況下生成。0 表示不限制。", "このセッションが 1 ターンでモデルに返せるツール出力のトークン総数です。そのターンのすべてのツール呼び出しを合計します。バジェットを使い切ると、残りの応答はツールなしで生成されます。0 は制限なしを意味します。"},
    {"Estimated input: %1 tokens", "預計輸入：%1 token", "推定入力：%1 トークン"},
    {"Approximate model-facing size of the request, using the app's character-based token estimate.", "請求的大致模型輸入規模，使用應用基於字元的 token 估算。", "リクエストのモデル向け入力サイズの概算です。アプリの文字ベースのトークン推定を使用します。"},
    {"Invalid Budget", "預算無效", "無効なバジェット"},
    {"The summarization content must be smaller than the summarization trigger. Otherwise an update would find nothing new to fold, and the trigger would fire again immediately.", "摘要內容必須小於摘要觸發閾值。否則更新時沒有可摺疊的新內容，觸發閾值會立即再次觸發。", "要約内容は要約トリガーより小さくなければなりません。そうでないと折り畳む新しい内容がなくなり、トリガーが直ちに再発火します。"},
    {"The response budget must not be greater than the summarization trigger. A reply could otherwise never complete before the conversation is merged again.", "回覆預算不得大於摘要觸發閾值。否則回覆可能永遠無法在對話再次合併之前完成。", "応答バジェットは要約トリガー以下でなければなりません。そうでないと、会話が再び統合される前に返信が完了できない可能性があります。"},
    {"<b>How conversation summarization works</b><br>Conversation summarization is a rolling record of older turns. A request carries the system prompt plus the transcript, always the newest messages that fit <i>Summarization trigger</i>. When the new transcript since the last update reaches that trigger, the summarizer reads from the first new message up to the newest <i>Summarization content</i> tokens and merges them into the summary. Keep it below the trigger so there is always something to fold. The message that crosses the boundary is kept whole.<br><br>The watermarks above are <b>bookkeeping only</b>: they count how much is new so the trigger can fire. They never decide what is sent or what is read — a summarization update always reads by recency, so pressing <b>Update summarization now</b> twice with no new messages reads the same set and produces the same summary (idempotent). Custom instructions are kept separate and are never touched.<br><br>The <b>assistant memory</b> is shared across every session: it is merged from the recent messages of all sessions (bounded by the assistant's own trigger) plus each session's summarization (bounded by the session-pool budget). Older sessions may therefore be forgotten. This session opts out of that sharing when <i>Include in assistant memory</i> is off.<br><br>Clearing the summary and saving resets its watermark to 0, re-admitting the whole transcript.", "<b>對話摘要的工作原理</b><br>對話摘要是較早輪次的滾動記錄。每次請求都會攜帶系統提示詞以及對話記錄，並且總是包含能放入 <i>摘要觸發閾值</i> 的最新訊息。當自上次更新以來的新對話達到該閾值時，摘要器會從第一條新訊息讀到最新的 <i>摘要內容</i> token 處，並將它們合併進摘要中。請讓它小於觸發閾值，以便總有內容可摺疊。跨越邊界的訊息會完整保留。<br><br>上方的水位<b>僅用於記帳</b>：它們統計有多少新內容，以便觸發閾值能夠觸發。它們絕不決定傳送或讀取什麼——摘要更新始終按最近程度讀取，因此在沒有新訊息時按兩次 <b>立即更新摘要</b> 會讀取同一組內容並產生相同的摘要（冪等）。自定義指令單獨儲存，永遠不會被觸碰。<br><br><b>助手記憶</b>在所有會話之間共享：它由所有會話的最近訊息（受助手自身觸發閾值限制）加上每個會話的摘要（受會話池預算限制）合併而成。因此較早的會話可能會被遺忘。關閉 <i>包含在助手記憶中</i> 時，此會話不參與該共享。<br><br>清空摘要並儲存會將其水位重置為 0，從而重新納入整個對話記錄。", "<b>会話要約の仕組み</b><br>会話要約は古いやり取りのローリング記録です。リクエストにはシステムプロンプトとトランスクリプトが含まれ、常に <i>要約トリガー</i> に収まる最新のメッセージが送信されます。前回の更新以降の新しいトランスクリプトがそのトリガーに達すると、要約器は最初の新しいメッセージから最新の <i>要約内容</i> トークンまでを読み取り、要約に統合します。トリガーより小さくしておくと、常に折り畳む内容があります。境界を越えるメッセージはそのまま保持されます。<br><br>上のウォーターマークは<b>記帳専用</b>です。どれだけ新しいか数えてトリガーを発火させるためだけに使用されます。何を送信し何を読み取るかを決定することはありません。要約の更新は常に新しい順に読み取るため、新しいメッセージがない状態で <b>今すぐ要約を更新</b> を2回押しても同じ集合を読み取り、同じ要約を生成します（冪等）。カスタム指示は別に保持され、決して変更されません。<br><br><b>アシスタントメモリ</b>はすべてのセッションで共有されます。これはすべてのセッションの最近のメッセージ（アシスタント自身のトリガーで上限）と各セッションの要約（セッションプールのバジェットで上限）から統合されます。そのため、古いセッションは忘れられる可能性があります。<i>アシスタントメモリに含める</i>がオフのとき、このセッションはその共有から外れます。<br><br>要約を消去して保存すると、ウォーターマークが 0 にリセットされ、トランスクリプト全体が再び取り込まれます。"},
    {"Session Busy", "會話繁忙", "セッションはビジーです"},
    {"A task is already running. Wait for it to finish, then update the summarization.", "已有任務正在執行。請等待其完成，然後更新摘要。", "タスクが既に実行中です。完了を待ってから要約を更新してください。"},
    // Presets.
    {"Instant", "極輕", "最軽量"},
    {"Very Light", "極輕", "最軽量"},
    {"Light", "輕量", "軽量"},
    {"Balanced", "均衡", "バランス"},
    {"Heavy", "重度", "ヘビー"},
    {"Very Heavy", "極重", "超ヘビー"},
    {"Ultra", "極致", "ウルトラ"},
    // Assistant form.
    {"Assistant name", "助手名稱", "アシスタント名"},
    {"Model id, e.g. gpt-4o", "模型 ID，例如 gpt-4o", "モデル ID（例: gpt-4o）"},
    {"Name", "名稱", "名前"},
    {"Model", "模型", "モデル"},
    {"Connection", "連線", "接続"},
    {"https://api.example.com/v1/chat/completions", "https://api.example.com/v1/chat/completions", "https://api.example.com/v1/chat/completions"},
    {"https://api.example.com/v1", "https://api.example.com/v1", "https://api.example.com/v1"},
    {"Authentication token", "身份驗證令牌", "認証トークン"},
    {"Optional", "可選", "任意"},
    {"API URL", "API URL", "API URL"},
    {"Token", "令牌", "トークン"},
    {"Proxy", "代理", "プロキシ"},
    {"Turn timeout", "回合超時", "ターンタイムアウト"},
    {"Applies to a new session's starting level and to assistant-wide memory updates, the shared memory across sessions. Chat turns and session summarization updates use the session's own level instead.", "適用於新會話的起始級別，以及助手級記憶更新（跨會話共享的記憶）。聊天回合和會話級摘要更新改用會話自身的級別。", "新しいセッションの開始レベルと、アシスタント全体のメモリ更新（セッション間で共有されるメモリ）に適用されます。チャットのターンとセッション単位の要約更新は、セッション自身のレベルを使用します。"},
    {"Memory update timeout", "記憶更新超時", "メモリ更新タイムアウト"},
    {"Seconds a request may wait for the next response byte before it is abandoned. Chat turns use the turn timeout; session summarization and assistant memory updates use the memory update timeout. Connecting and uploading keep a fixed 60-second window.", "請求在放棄前等待下一個響應位元組的秒數。聊天回合使用回合超時；會話摘要和助手記憶更新使用記憶更新超時。連線和上傳保持固定的 60 秒視窗。", "リクエストが破棄されるまでに次の応答バイトを待つ秒数。チャットのターンはターンタイムアウトを、セッションの要約およびアシスタントのメモリ更新はメモリ更新タイムアウトを使用します。接続とアップロードは固定の 60 秒ウィンドウのままです。"},
    {"Maximum silence, in seconds, while a chat turn streams its reply.", "聊天回合流式回覆時允許的最長靜默秒數。", "チャットのターンが応答をストリーミングする際に許容される最大の無音秒数。"},
    {"Maximum silence, in seconds, while a session summarization or assistant memory update runs. Reasoning models often need longer.", "會話摘要或助手記憶更新執行期間允許的最長靜默秒數。推理模型通常需要更長時間。", "セッションの要約またはアシスタントのメモリ更新の実行中に許容される最大の無音秒数。推論モデルではより長く必要になることがよくあります。"},
    {"System prompt for this assistant", "此助手的系統提示詞", "このアシスタントのシステムプロンプト"},
    {"Fill the memory trigger and budget fields below with a preset. Nothing is saved until you press Save.", "使用預設填充下面的記憶觸發閾值與預算欄位。在你按下“儲存”之前不會儲存任何內容。", "プリセットで下のメモリトリガーとバジェットの項目を入力します。「保存」を押すまで何も保存されません。"},
    {"Target size of the generated assistant memory.", "生成的助手記憶的目標大小。", "生成されるアシスタントメモリの目標サイズ。"},
    {"Extra completion budget for a reasoning model's hidden thinking, so the visible memory is not starved.", "為推理模型的隱藏思考預留的額外補全預算，以免可見記憶被擠佔。", "推論モデルの隠れた思考のための追加の補完バジェット。可視メモリが逼迫しないようにします。"},
    {"New tokens across all sessions that trigger a memory update; also the cap on the recent messages gathered.", "所有會話中觸發記憶更新的新 token 總數；同時也是所收集最近訊息的上限。", "メモリの更新を起動する全セッションの新規トークン数。収集される最近のメッセージの上限でもあります。"},
    {"Token cap on the per-session summarizations included in the assistant memory.", "包含在助手記憶中的每個會話摘要的 token 上限。", "アシスタントメモリに含まれるセッションごとの要約のトークン上限。"},

    {"Session pool", "會話池", "セッションプール"},
    // Tasks window.
    {"%1 s", "%1 秒", "%1 秒"},
    {"This task produced no log.", "此任務未生成日誌。", "このタスクはログを生成しませんでした。"},
    {"Select a task to read its log.", "選擇一個任務以檢視其日誌。", "ログを確認するタスクを選択してください。"},
    {"Copy log", "複製日誌", "ログをコピー"},
    {"Request", "請求", "リクエスト"},
    {"Response", "響應", "応答"},
    {"Tool call", "工具呼叫", "ツール呼び出し"},
    {"Tool result", "工具結果", "ツール結果"},
    {"Reasoning", "推理", "推論"},
    {"Failure", "失敗", "失敗"},
    {"Log", "日誌", "ログ"},
    {"Round", "輪次", "ラウンド"},
    {"%1 characters", "%1 個字元", "%1 文字"},
    {"Model: %1", "模型：%1", "モデル: %1"},
    {"Tasks", "任務", "タスク"},
    {"Running and recently finished generation tasks. Select one to read its log.", "正在執行和最近完成的生成任務。選擇一項以檢視其日誌。", "実行中および最近完了した生成タスク。項目を選択するとログを確認できます。"},
    {"No tasks yet.", "暫無任務。", "タスクはまだありません。"},
    {"%1   ·   Round %2   ·   %3", "%1   ·   第 %2 輪   ·   %3", "%1   ·   ラウンド %2   ·   %3"},
    {"Input %1   ·   Output %2", "輸入 %1   ·   輸出 %2", "入力 %1   ·   出力 %2"},
    {"response budget", "回覆預算", "応答バジェット"},
    {"summarization budget", "摘要預算", "要約バジェット"},
    {"memory budget", "記憶預算", "メモリバジェット"},
    {", charged against the %1", "，計入%1", "（%1に計上）"},
    {"Input: prompt tokens billed by every request of this task, summed over its tool rounds. Output: tokens generated plus the tool results fed back%1. Tool output is always counted at an estimated cost; a leading ~ marks a task whose request usage was estimated.", "輸入：此任務的每次請求所計費的提示詞 token，按工具輪次累計。輸出：生成的 token 加上回傳的工具結果%1。工具輸出始終按估算成本計算；開頭的 ~ 表示請求用量為本地估算的任務。", "入力：このタスクの各リクエストで課金されたプロンプトトークン（ツールラウンド分を合計）。出力：生成されたトークンと返されたツール結果%1。ツール出力は常に推定コストで計上されます。先頭の ~ はリクエストの使用量が推定されたタスクを示します。"},
    // Summary review dialog.
    {"Review summarization update", "審查摘要更新", "要約更新の確認"},
    {"The following request is about to be sent to the model to update the %1. Review it and choose whether to run the summarization update.", "以下請求即將傳送給模型以更新 %1。請審閱，並選擇是否執行摘要更新。", "以下のリクエストが %1 を更新するためにモデルへ送信されようとしています。確認して、要約の更新を実行するか選択してください。"},
    {"This is the first of several requests: the remaining messages are merged in further batches after this one.", "這是多個請求中的第一個：其餘訊息將在本次之後分批進一步合併。", "これは複数のリクエストの最初のものです。残りのメッセージはこの後さらにバッチで統合されます。"},
    {"Summarization Update Skipped", "已跳過摘要更新", "要約の更新をスキップしました"},
    {"The automatic summarization update was skipped. Some messages may not be merged into the summary; use Update summarization now before sending new messages if you want them included.", "自動摘要更新已跳過。部分訊息可能尚未合併到摘要中；若希望它們被包含，請在傳送新訊息前使用“立即更新摘要”。", "自動要約更新がスキップされました。一部のメッセージが要約に統合されていない可能性があります。含めたい場合は、新しいメッセージを送信する前に「今すぐ要約を更新」を使用してください。"},
    {"Model: %1\n", "模型：%1\n", "モデル: %1\n"},
    {"Completion cap: %1 tokens\n\n", "補全上限：%1 個 token\n\n", "補完上限: %1 トークン\n\n"},
    // Tool status.
    {"Reading files...", "正在讀取檔案...", "ファイルを読み込み中..."},
    {"Searching code...", "正在搜尋程式碼...", "コードを検索中..."},
    {"Listing files...", "正在列出檔案...", "ファイルを一覧表示中..."},
    {"Finding files...", "正在查詢檔案...", "ファイルを検索中..."},
    {"Writing file...", "正在寫入檔案...", "ファイルに書き込み中..."},
    {"Editing file...", "正在編輯檔案...", "ファイルを編集中..."},
    {"Deleting file...", "正在刪除檔案...", "ファイルを削除中..."},
    {"Fetching page...", "正在獲取頁面...", "ページを取得中..."},
    {"Searching the web...", "正在搜尋網路...", "ウェブを検索中..."},
    {"Using tools...", "正在使用工具...", "ツールを使用中..."},
    // Log inspector.
    {"Logs", "日誌", "ログ"},
    {"Info", "資訊", "情報"},
    {"Debug", "除錯", "デバッグ"},
    {"Error", "錯誤", "エラー"},
    {"Level", "級別", "レベル"},
    {"All levels", "所有級別", "すべてのレベル"},
    {"Live records from the running application.", "執行中應用的實時記錄。", "実行中アプリケーションのライブ記録。"},
    {"No log records yet.", "暫無日誌記錄。", "ログ記録はまだありません。"},
    {"Select a log entry to read it.", "選擇一條日誌以檢視。", "ログ項目を選択して表示します。"},
    {"Copy entry", "複製條目", "項目をコピー"},
    {"Clear", "清空", "クリア"},
    {"%1 records", "%1 條記錄", "%1 件の記録"},
    {"%1   ·   %2", "%1   ·   %2", "%1   ·   %2"},
    // Log inspector: rendered request payloads.
    {"Payload", "載荷", "ペイロード"},
    {"LLM request", "LLM 請求", "LLM リクエスト"},
    {"model", "模型", "モデル"},
    {"items", "項", "項目"},
    {"Messages", "訊息", "メッセージ"},
    {"Parameters", "引數", "パラメーター"},
    {"(empty)", "（空）", "（空）"},
    {"(none)", "（無）", "（なし）"},
    {"tool call", "工具呼叫", "ツール呼び出し"},
    {"tool output", "工具輸出", "ツール出力"},
    {"reasoning", "推理", "推論"},
    {"hidden", "已隱藏", "非表示"},
    {"%1 input", "%1 項輸入", "入力 %1 件"},
    {"%1 tools", "%1 個工具", "ツール %1 件"},
    {"Raw", "原始", "生データ"},

    // Remaining UI strings (assistant list, session editor, settings, composer).
    {"%1 / %2 tok", "%1 / %2 tok", "%1 / %2 tok"},
    {"(no output)", "（無輸出）", "（出力なし）"},
    {"API protocol", "API 協議", "API プロトコル"},
    {"Appearance and startup behaviour for the whole app.", "整個應用的外觀與啟動行為。", "アプリ全体の外観と起動時の動作。"},
    {"Chat Completions", "Chat Completions", "Chat Completions"},
    {"Choose an assistant", "選擇助手", "アシスタントを選択"},
    {"Common", "通用", "共通"},
    {"Compact threshold", "壓縮閾值", "圧縮しきい値"},
    {"Configuration of the open assistant.", "當前開啟的助手的配置。", "開いているアシスタントの設定。"},
    {"Ctrl+Enter sends; Enter inserts a newline", "Ctrl+Enter 傳送；Enter 換行", "Ctrl+Enter で送信、Enter で改行"},
    {"Custom Instructions", "自定義指令", "カスタム指示"},
    {"Delete", "刪除", "削除"},
    {"Delete \"%1\" and its whole notebook? This cannot be undone.", "刪除“%1”及其整個筆記本？此操作無法撤銷。", "「%1」とそのノートブック全体を削除しますか？この操作は元に戻せません。"},
    {"Delete assistant", "刪除助手", "アシスタントを削除"},
    {"Delete this session and its whole history? This cannot be undone.", "刪除此會話及其全部歷史？此操作無法撤銷。", "このセッションとその履歴全体を削除しますか？この操作は元に戻せません。"},
    {"Directories this session can read and the tools it may use.", "此會話可讀取的目錄及可使用的工具。", "このセッションが読み取れるディレクトリと使用できるツール。"},
    {"Each assistant is a separate notebook.", "每個助手是一個獨立的筆記本。", "各アシスタントは独立したノートブックです。"},
    {"Extra guidance sent with every turn of this session.", "隨此會話每一回合一同傳送的額外指引。", "このセッションの毎ターンに送信される追加の指針。"},
    {"Go to the previous prompt", "跳到上一條提示", "前のプロンプトへ移動"},
    {"Local Notebook", "Local Notebook", "Local Notebook"},
    {"Local Notebook\nA local-first notebook for chat assistants.", "Local Notebook\n本地優先的聊天助手筆記本。", "Local Notebook\nローカル優先のチャットアシスタント用ノートブック。"},
    {"New", "新建", "新規"},
    {"One directory per line", "每行一個目錄", "1行に1ディレクトリ"},
    {"One directory per line. They are mounted into the session's tools.", "每行一個目錄。它們會掛載到會話的工具中。", "1行に1ディレクトリ。セッションのツールにマウントされます。"},
    {"Online (web search)", "線上（網路搜尋）", "オンライン（ウェブ検索）"},
    {"Open this assistant automatically next time", "下次自動開啟此助手", "次回このアシスタントを自動で開く"},
    {"Preserve tail", "保留尾部", "末尾を保持"},
    {"Responses API", "Responses API", "Responses API"},
    {"Run commands", "執行命令", "コマンドを実行する"},
    {"Select a session, or create a new one.", "選擇一個會話，或新建一個。", "セッションを選択するか、新規作成します。"},
    {"Summary budget", "摘要預算", "要約バジェット"},
    {"Title, reasoning and the budgets this session runs with.", "此會話使用的標題、推理與預算。", "このセッションで使用するタイトル、推論、バジェット。"},
    {"Typography", "字型排印", "タイポグラフィ"},
    {"Untitled", "未命名", "無題"},
};

const std::unordered_map<std::string_view, Entry>& table() {
  static const std::unordered_map<std::string_view, Entry> map = [] {
    std::unordered_map<std::string_view, Entry> built;
    built.reserve(std::size(kTranslations));
    for (const Entry& entry : kTranslations) {
      built.emplace(entry.en, entry);
    }
    return built;
  }();
  return map;
}

Language g_language = Language::English;

QString from_utf8(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

} // namespace

Language language_from_code(std::string_view code) {
  // Locale codes first; the bare legacy codes are accepted so a settings file
  // written before the rename still selects the same language.
  if (code == "zh_cn" || code == "zh_tw" || code == "zh_hk" || code == "zh") {
    return Language::ChineseTraditional;
  }
  if (code == "ja_jp" || code == "ja") {
    return Language::Japanese;
  }
  return Language::English;
}

std::string_view code_from_language(Language language) {
  switch (language) {
  case Language::ChineseTraditional:
    return "zh_cn";
  case Language::Japanese:
    return "ja_jp";
  case Language::English:
    break;
  }
  return "en_us";
}

QString language_name(Language language) {
  switch (language) {
  case Language::ChineseTraditional:
    return QString::fromUtf8("繁體中文");
  case Language::Japanese:
    return QString::fromUtf8("日本語");
  case Language::English:
    break;
  }
  return QStringLiteral("English");
}

void set_language(Language language) { g_language = language; }

Language language() { return g_language; }

QString translate(std::string_view source) {
  if (g_language == Language::English) {
    return from_utf8(source);
  }

  const auto found = table().find(source);
  if (found == table().end()) {
    return from_utf8(source);
  }

  const char* value = g_language == Language::ChineseTraditional ? found->second.zh
                                                                : found->second.ja;
  return value != nullptr ? QString::fromUtf8(value) : from_utf8(source);
}

} // namespace lnb::i18n
