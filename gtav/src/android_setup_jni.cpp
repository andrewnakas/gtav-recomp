// gtav - libgtav_setup.so: the Android app's setup screen (SetupActivity.java) runs the game data
// install here, the same code the desktop and iPhone installers use (loader/src/install.cpp
// RunDataInstall: Disc 2 + TU27 + the Disc 1 install partitions, every file checked against the
// pack's expected.txt, resumable). This library has no game code and no dependency on it: it loads
// before the player's libgtav_game.so exists.
//
// The discs come from the Storage Access Framework: Java opens each document and passes
// /proc/self/fd/<fd>, which the installer reads like any file (ISO images and the TU package).

#include <android/log.h>
#include <jni.h>

#include <filesystem>
#include <string>

#include "../../loader/src/install.h"

namespace {

std::wstring W(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string u(c);
  env->ReleaseStringUTFChars(s, c);
  return std::filesystem::path(u).wstring();
}

std::string N(const std::wstring& s) { return std::filesystem::path(s).string(); }

// InstallUi -> the Java object's callbacks (called on the install thread, a Java thread).
class JavaUi : public loader::InstallUi {
 public:
  JavaUi(JNIEnv* env, jobject obj) : env_(env), obj_(obj) {
    jclass c = env->GetObjectClass(obj);
    stage_ = env->GetMethodID(c, "onStage", "(Ljava/lang/String;)V");
    log_ = env->GetMethodID(c, "onLog", "(Ljava/lang/String;)V");
    progress_ = env->GetMethodID(c, "onProgress", "(D)V");
    cancelled_ = env->GetMethodID(c, "isCancelled", "()Z");
    ask_ = env->GetMethodID(c, "onAsk", "(Ljava/lang/String;)Z");
  }
  void Stage(const std::wstring& t) override { CallString(stage_, t); }
  void Log(const std::wstring& t) override {
    __android_log_print(ANDROID_LOG_INFO, "gtav-setup", "%s", N(t).c_str());
    CallString(log_, t);
  }
  void Progress(double f) override {
    int v = int(f * 1000);
    if (v == last_) return;
    last_ = v;
    env_->CallVoidMethod(obj_, progress_, jdouble(f));
  }
  bool Cancelled() override { return env_->CallBooleanMethod(obj_, cancelled_); }
  bool Ask(const std::wstring& q) override {
    jstring s = env_->NewStringUTF(N(q).c_str());
    bool yes = env_->CallBooleanMethod(obj_, ask_, s);
    env_->DeleteLocalRef(s);
    return yes;
  }

 private:
  void CallString(jmethodID m, const std::wstring& t) {
    jstring s = env_->NewStringUTF(N(t).c_str());
    env_->CallVoidMethod(obj_, m, s);
    env_->DeleteLocalRef(s);
  }
  JNIEnv* env_;
  jobject obj_;
  jmethodID stage_, log_, progress_, cancelled_, ask_;
  int last_ = -1;
};

}  // namespace

extern "C" {

// Returns null when the install finished, else the error text.
JNIEXPORT jstring JNICALL Java_com_gtavrecomp_app_SetupActivity_nativeInstall(
    JNIEnv* env, jobject self, jstring home, jstring expected, jstring disc1, jstring disc2, jstring tu) {
  JavaUi ui(env, self);
  loader::InstallInputs in{W(env, disc1), W(env, disc2), W(env, tu)};
  std::wstring error;
  bool ok = loader::RunDataInstall(std::filesystem::path(W(env, home)), std::filesystem::path(W(env, expected)),
                                   in, ui, &error);
  if (ok) return nullptr;
  return env->NewStringUTF(N(error.empty() ? std::wstring(L"The install stopped.") : error).c_str());
}

// Game files copied from a PC install (USB) instead of the discs. null when they check out.
JNIEXPORT jstring JNICALL Java_com_gtavrecomp_app_SetupActivity_nativeAdopt(JNIEnv* env, jobject self, jstring home,
                                                                           jstring expected) {
  JavaUi ui(env, self);
  std::wstring error;
  if (loader::AdoptCopiedData(std::filesystem::path(W(env, home)), std::filesystem::path(W(env, expected)), ui,
                              &error))
    return nullptr;
  return env->NewStringUTF(N(error.empty() ? std::wstring(L"The check stopped.") : error).c_str());
}

JNIEXPORT jboolean JNICALL Java_com_gtavrecomp_app_SetupActivity_nativeIsDataInstalled(JNIEnv* env, jclass,
                                                                                         jstring home) {
  return loader::IsDataInstalled(std::filesystem::path(W(env, home)));
}

}  // extern "C"
