# قرارداد مشترک فایل‌ها و اجرای RoboRace

این فایل مرجع فنی همراه دو فاز است. نمونه‌ها در پوشه examples داده‌های آموزشی ساخته‌شده برای نمایش قالب‌اند؛ خروجی اجرای واقعی برنامه دانشجویان نیستند. تحویل نهایی دانشجویان باید لاگ اجرای واقعی کد خودشان باشد.

## قالب سناریو
فایل‌های پوشه scenarios با پسوند .race، محتوای JSON و کدگذاری UTF-8 هستند. همه فیلدهای نمونه الزامی‌اند. schema_version برابر 1 است. track_length، tick_ms و max_ticks صحیح مثبت و seed صحیح نامنفی است؛ seed در رفتار دو فاز فعلی اثر ندارد. robots دست‌کم دو عضو با شناسه یکتا دارد؛ actions آرایه دستورهای MOVE یا OPEN:<chest_id> است و می‌تواند خالی باشد. شناسه‌ها فقط حروف لاتین، رقم و خط زیر دارند؛ SERVER و CONTROLLER و LAUNCHER رزروشده‌اند.
صندوق شامل id، position، type=TURBO و boost_moves است؛ position از صفر تا track_length و boost_moves صحیح مثبت است. پل شامل id، start، end و capacity است؛ start دست‌کم 1، end دست‌کم start و کمتر از track_length، و capacity صحیح مثبت است. پل‌ها هم‌پوشانی ندارند و شناسه تمام منابع یکتا است. هر OPEN به یک صندوق موجود اشاره می‌کند. کلید اختیاری اضافی نادیده گرفته می‌شود. JSON یا ساختار نامعتبر رد می‌شود.

## انتقال پیام
هر پیام یک شیء JSON در یک خط با UTF-8 و پایان LF است؛ حداکثر طول خط 65536 بایت است. دریافت تکه‌تکه و چند پیام چسبیده در TCP باید با بافر مدیریت شود. فایل messages.jsonl یک رونوشت ترکیبی آموزشی از دو جهت تبادل است؛ جهت از sender مشخص می‌شود. در ارسال واقعی فقط پیام فرستنده روی همان اتصال فرستاده می‌شود. اعلان عمومی در رونوشت فقط یک بار آمده و روی شبکه به همه دریافت‌کنندگان مقرر ارسال می‌شود.

## جزئیات فاز 1

۱۶  پیوست ب  نمونه پیام و فایل خروجی

قرارداد مشترک پیام‌ها در ادامه آمده است. همه درخواست‌ها type، race_id، sender، request_id و payload دارند؛ sender ربات برابر id خودش و برای Controller برابر CONTROLLER است. شناسه درخواست در کل نشست یکتا است. پاسخ‌های مستقیم reply_to همان request_id را دارند؛ اعلان عمومی reply_to ندارد. پیام‌های هر اتصال به‌ترتیب پردازش می‌شوند و هر اتصال فقط به یک شناسه اختصاص دارد.

SCENARIO_UPLOAD → SCENARIO_ACCEPTED: payload درخواست یک شیء با کلید scenario و مقدار برابر شیء کامل فایل است؛ payload پاسخ {} است. Race Server فقط یک سناریو می‌پذیرد. Launcher پس از تأیید Controller از طریق یک کانال محلی ساده، Robot Clientها را ایجاد می‌کند؛ این کانال می‌تواند Pipe باشد و قالب آن در README گروه توضیح داده می‌شود.

REGISTER_ROBOT → REGISTERED: payload برابر {"robot_id":"R1"} و پاسخ {} است؛ robot_id باید در سناریو باشد و با sender یکسان باشد. READY → READY_ACCEPTED و CLIENT_DONE → DONE_ACCEPTED هر دو payload={} دارند. READY فقط پس از ثبت‌نام و CLIENT_DONE فقط پس از START و دریافت تمام پاسخ‌های عملیات مجاز است.

ACTION_REQUEST → STATE_UPDATE: برای حرکت payload={"action":"MOVE"} و برای صندوق payload={"action":"OPEN","resource":"C1"} است. فاز اول payload پاسخ را {"status":"RECORDED"} می‌گذارد. START و SESSION_FINISHED اعلان با payload={} هستند. READY_ACCEPTED پیش از START همان اتصال ارسال می‌شود؛ پس از پایان نشست اتصال‌ها بسته می‌شوند.

ERROR پاسخ مستقیم با payload={"code":"INVALID_MESSAGE","message":"..."} است. code یکی از INVALID_JSON، INVALID_MESSAGE، INVALID_SCENARIO، DUPLICATE_ID یا INVALID_STATE است. اگر request_id معتبر خوانده نشود reply_to=null است. شناسه یا شناسه درخواست تکراری اجرا نمی‌شود؛ خطای فریم، JSON یا شناسه تکراری پس از ثبت خطا اتصال را می‌بندد. سایر خطاهای درخواست معتبر را کلاینت گزارش می‌کند و نشست با وضعیت ناموفق خاتمه می‌یابد. در قطع اتصال، فرستادن پاسخ روی سوکت بسته الزامی نیست.

{"type":"ACTION_REQUEST","race_id":"demo01",
 "sender":"R1","request_id":"R1-03",
 "payload":{"action":"OPEN","resource":"C1"}}
{"type":"STATE_UPDATE","race_id":"demo01",
 "sender":"SERVER","reply_to":"R1-03",
 "payload":{"status":"RECORDED"}}

سطرهای زیر گزیده‌ای از events.log و گزارش متنی Verifier هستند و اجرای کامل نیستند. Log واقعی باید تمام درخواست‌ها، پاسخ‌ها و رویدادهای چرخه اجرا را طبق قرارداد پیوست ثبت کند.

tick=-1 actor=R1 event=REGISTER resource=- result=OK req=R1-01
tick=-1 actor=SERVER event=RESPOND resource=- result=REGISTERED req=R1-01
tick=-1 actor=R1 event=READY resource=- result=OK req=R1-02
tick=-1 actor=SERVER event=RESPOND resource=- result=READY_ACCEPTED req=R1-02
tick=-1 actor=SERVER event=START resource=- result=OK req=-
tick=-1 actor=R1 event=RECEIVE resource=C1 result=OPEN req=R1-05
tick=-1 actor=SERVER event=RESPOND resource=C1 result=RECORDED req=R1-05

schema=1 race_id=demo01 phase=1 status=PASS
check=ready_before_start result=PASS
check=one_response_per_request result=PASS

۱۷  پیوست پ  قرارداد دقیق ثبت رویدادها

UPLOAD، REGISTER، READY و DONE با actor فرستنده، resource=-، result=OK و req شناسه درخواست ثبت می‌شوند. هر ACTION_REQUEST پذیرفته‌شده رویداد RECEIVE با actor ربات، result برابر MOVE یا OPEN، resource برابر TRACK یا id صندوق و req همان شناسه دارد. پاسخ مستقیم با RESPOND و actor=SERVER ثبت می‌شود؛ result نام نوع پاسخ است، به‌جز STATE_UPDATE که status آن ثبت می‌شود. این دو رویداد برای نگاشت درخواست به پاسخ کافی‌اند.

START و SESSION_END با actor=SERVER، resource=-، req=- و result=OK ثبت می‌شوند؛ در پایان ناموفق result=ABORTED است. ERROR با actor فرستنده، resource=-، result برابر code و req شناسه قابل‌استخراج یا - ثبت می‌شود؛ اگر ERROR روی شبکه فرستاده شود RESPOND با result=ERROR نیز ثبت می‌شود. DISCONNECT با actor همان اتصال، resource=-، result=EOF یا TIMEOUT و req=- ثبت می‌شود. هر درخواست بدون پاسخ در قطع اتصال با CANCEL، actor ربات، resource منبع درخواست، result=DISCONNECTED و req مربوط خاتمه می‌یابد.

در قطع اتصال پیش از پایان عادی، نشست فاز اول ABORTED می‌شود؛ پاسخ باقی‌مانده درخواست‌های قابل‌ارسال ERROR با code=INVALID_STATE است. Verifier در اجرای عادی وجود یک RESPOND برای هر درخواست را می‌سنجد؛ در اجرای قطع اتصال، یک CANCEL به‌جای پاسخ روی سوکت بسته مجاز است و مورد بی‌سرانجام نباید باقی بماند. REQUEST تکراری نامعتبر، درخواست پذیرفته‌شده جدید محسوب نمی‌شود.

گزارش verify_report.txt با schema=1 race_id=<id> phase=1 status=PASS یا FAIL شروع می‌شود؛ سپس برای هر کنترل یک سطر check=<name> result=PASS یا FAIL دارد. نام کنترل‌های اجباری: unique_registration، ready_before_start، one_response_per_request، no_action_before_start و clean_session_end. status فقط وقتی PASS است که همه کنترل‌ها PASS باشند؛ در آزمون قطع اتصال، clean_session_end پاک‌سازی و ثبت ABORTED را می‌سنجد.

## جزئیات فاز 2

۱۵  پیوست  نمونه خروجی و قرارداد فایل‌ها

قرارداد مشترک پیام‌ها و Log در فاز اول تعریف شده و در این فاز تکمیل می‌شود. STATE_UPDATE الزاماً status، position، tick و boost_remaining دارد. status یکی از SUCCESS، FAILED، BLOCKED یا CANCELLED است. START علاوه بر payload={} فاز اول، در این فاز payload={"tick":0} دارد. RACE_FINISHED اعلان پایان بازی با winner، tick و reason است؛ winner در پایان بدون برنده null است. نمونه‌های چندخطی هنگام ارسال در یک خط قرار می‌گیرند.

{"type":"STATE_UPDATE","race_id":"demo01",
 "sender":"SERVER","reply_to":"R1-04",
 "payload":{"status":"SUCCESS","position":2,
 "tick":2,"boost_remaining":0}}
{"type":"RACE_FINISHED","race_id":"demo01",
 "sender":"SERVER",
 "payload":{"winner":"R1","tick":6,"reason":"WINNER"}}

برای آزمایش صندوق مشترک، فایل کامل chest_race.race زیر قابل استفاده است. در حالت unsafe باید هم‌زمانی اجرای OPEN با دو نخ کارگر آزمایش شود.

{
  "schema_version": 1, "race_id": "chest_race",
  "track_length": 4, "tick_ms": 250,
  "max_ticks": 16, "seed": 3,
  "robots": [
    {"id":"R1", "actions":["MOVE","MOVE","OPEN:C1","MOVE"]},
    {"id":"R2", "actions":["MOVE","MOVE","OPEN:C1","MOVE"]}
  ],
  "chests": [
    {"id":"C1", "position":2,
     "type":"TURBO", "boost_moves":2}
  ],
  "bridges": []
}

سطرهای زیر گزیده‌ای از لاگ آزمایش صندوق هستند؛ لاگ کامل شامل REGISTER، READY، RECEIVE، پاسخ‌ها و تمام MOVEهای هر دو ربات است. در unsafe هر دو OPEN در Tick=3 با SUCCESS ثبت می‌شوند و Verifier باید FAIL بدهد؛ نمونه زیر حالت safe را نشان می‌دهد.

tick=3 actor=R1 event=OPEN resource=C1 result=SUCCESS req=R1-05
tick=3 actor=SERVER event=RESPOND resource=C1 result=SUCCESS req=R1-05
tick=3 actor=R2 event=OPEN resource=C1 result=FAILED req=R2-05
tick=3 actor=SERVER event=RESPOND resource=C1 result=FAILED req=R2-05
tick=4 actor=R1 event=MOVE resource=4 result=SUCCESS req=R1-06
tick=4 actor=R1 event=FINISH resource=TRACK result=SUCCESS req=-

schema=1 race_id=chest_race phase=2 status=PASS
check=single_chest_owner result=PASS
check=valid_movement result=PASS
check=bridge_capacity result=PASS
check=one_winner result=PASS
check=request_completion result=PASS
check=resource_cleanup result=PASS
check=thread_lifecycle result=PASS

نام و قالب فایل‌های تحویلی ثابت است: demo01.race و سایر سناریوها در JSON، پیام‌های TCP به‌صورت JSON Lines، فایل events.log با فیلدهای فوق و verify_report.txt با خطوط schema و check. افزودن فیلد اختیاری مجاز است، اما حذف یا تغییر معنای فیلدهای الزامی مجاز نیست.

۱۶  مرز Tick و پایان مسابقه

در آغاز هر دوره، سرور از هر ربات متصل که CLIENT_DONE نداده است، دقیقاً یک ACTION_REQUEST یا CLIENT_DONE جمع می‌کند. دوره جمع‌آوری زودتر از tick_ms میلی‌ثانیه بسته نمی‌شود؛ اگر همه ورودی‌ها نرسیده باشند تا timeout برابر بیشینه 5000 میلی‌ثانیه و دو برابر tick_ms منتظر می‌ماند. ربات دیرکرده با TIMEOUT قطع و پاک‌سازی می‌شود. سپس مجموعه ثابت درخواست‌های این دوره با دو یا چند نخ کارگر پردازش می‌شود؛ هیچ رباتی در همان دوره دو عملیات ندارد.

Handlerها درخواست را در Active Queue ثبت می‌کنند؛ کارگرها فقط پس از بسته‌شدن جمع‌آوری دوره اجازه برداشت دارند. هر کلاینت پس از STATE_UPDATE دستور بعدی یا CLIENT_DONE را می‌فرستد؛ درخواست‌های زودرس بعدی برای دوره بعد نگه داشته می‌شوند. FAILED دستور را مصرف می‌کند، BLOCKED فقط در MOVE دستور را نگه می‌دارد و در دوره بعد تکرار می‌شود. خطای ساختار پیام ERROR است و با شکست معتبر بازی تفاوت دارد.

دوره آخر شامل max_ticks نیز کامل پردازش می‌شود؛ اگر برنده‌ای ثبت نشد، پایان با reason=MAX_TICKS است. اگر همه ربات‌ها تمام یا قطع شوند، پایان بدون برنده با reason=NO_ACTIVE_ROBOTS است. در برد reason=WINNER است. RACE_FINISHED برای همه اتصال‌های زنده از جمله Controller ارسال می‌شود و کلاینت‌ها پس از آن دستور تازه نمی‌فرستند. همه درخواست‌های قبلاً پذیرفته‌شده با نتیجه هنوز ثبت‌نشده با پاسخ CANCELLED تعیین تکلیف می‌شوند؛ برای اتصال قطع‌شده CANCEL ثبت می‌شود.

سرور پس از پاک‌سازی منابع و join همه نخ‌های کارگر و رسیدگی‌کننده ربات‌ها، SESSION_FINISHED با payload={} را برای Controller می‌فرستد، SESSION_END را ثبت می‌کند و بسته می‌شود. Controller تا این اعلان باز می‌ماند؛ نخ آن پس از ارسال اعلان خاتمه می‌یابد. Launcher همه پردازه‌ها را با waitpid جمع‌آوری می‌کند. برای هر اتصال ارسال پیام‌ها باید سریال و بدون درهم‌رفتن بایت‌ها باشد.

۱۷  قرارداد شواهد و بررسی صحت

MOVE با actor ربات، resource برابر موقعیت عددی پس از عملیات، result برابر SUCCESS یا BLOCKED یا CANCELLED و req شناسه درخواست ثبت می‌شود؛ موقعیت اولیه صفر است. OPEN با resource=id صندوق، result=SUCCESS یا FAILED یا CANCELLED ثبت می‌شود؛ FAILED به‌معنای موقعیت نادرست یا صندوق واگذارشده است. هر درخواست یک نتیجه عملیات و سپس یک RESPOND با همان status دارد. RECEIVE قرارداد فاز اول را حفظ می‌کند و tick آن دوره هدف است.

BRIDGE_ENTER و BRIDGE_EXIT با actor ربات، resource=id پل، result=OK و req شناسه MOVE ثبت می‌شوند. برای حرکت از p به q، هر پل که مسیر حرکت پس از p تا خود q آن را قطع کند باید ظرفیت داشته باشد؛ اگر p داخل پل باشد ربات از قبل ظرفیت دارد. عبور کامل در یک MOVE شامل ENTER و EXIT در همان عملیات است؛ توقف در پل ظرفیت را نگه می‌دارد و خروج فقط وقتی موقعیت جدید از end بیشتر باشد. ترتیب این رویدادها و MOVE زیر قفل وضعیت ثبت می‌شود و شکست حرکت هیچ تغییری ایجاد نمی‌کند.

در DISCONNECT یا پایان بازی، BRIDGE_EXIT برای هر ظرفیت باقی‌مانده با result=DISCONNECTED یا SHUTDOWN و req=- ثبت می‌شود. CANCEL با resource منبع اصلی، req شناسه درخواست و result=DISCONNECTED برای سوکت بسته ثبت می‌شود. FINISH با actor برنده، resource=TRACK، result=SUCCESS و req=- دقیقاً یک‌بار و بعد از MOVE برنده است. RACE_END با actor=SERVER، resource=winner یا -، result برابر reason و req=- ثبت می‌شود؛ پس از آن تغییر جدید بازی مجاز نیست و فقط لغو و پاک‌سازی انجام می‌شود.

THREAD_START و THREAD_END با actor=SERVER، resource شناسه یکتای نخ مانند H_R1 یا W_1، result=OK و req=- ثبت می‌شوند؛ END پس از آخرین کار نخ ثبت می‌شود. نخ اصلی پس از join کارگرها و Handlerهای ربات، JOIN با همان resource و result=OK ثبت می‌کند. Handler کنترل با H_CONTROLLER مشخص می‌شود؛ END آن پس از SESSION_END و در آخرین مرحله پیش از خروج سرور ثبت می‌شود. بررسی‌کننده تطابق START و END و JOIN نخ‌های ربات و کارگر را می‌سنجد؛ خاتمه واقعی را Launcher کنترل می‌کند.

گزارش فاز دوم همه کنترل‌های فاز اول و کنترل‌های single_chest_owner، valid_movement، bridge_capacity، one_winner، request_completion، resource_cleanup و thread_lifecycle را دارد. Verifier موقعیت و تقویت را از سناریو و نتیجه عملیات بازسازی می‌کند، ظرفیت را از ENTER/EXIT می‌شمارد و درخواست‌ها را با req تطبیق می‌دهد. قانون یکتایی صندوق در هر دو حالت بررسی می‌شود؛ unsafe آن را دور نمی‌زند. در سناریوی بدون پل، bridge_capacity پس از بررسی خالی‌بودن منابع PASS است.
