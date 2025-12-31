# EchoLens 🔍

EchoLens is a high-performance **research and discovery application** built with **Flutter**.  
It leverages **Google Gemini AI with Google Search Grounding** to deliver accurate, real-time, and **source-backed information**, making it ideal for deep research, analysis, and report generation.

Designed with a **sleek dark-themed UI**, EchoLens provides a professional and distraction-free research environment.

---

## ✨ Key Features

- **AI-Powered Research**  
  Uses **Gemini 2.5 Flash with Search Grounding** to ensure factual, reference-based answers.

- **Source Transparency**  
  Automatically displays the list of web sources used to generate responses.

- **Daily Usage Limiter**  
  Smart daily limit of **3 searches per user**, stored in Firestore to control API costs.

- **Admin Exemption**  
  Built-in bypass for a specified **Admin email** with unlimited searches.

- **Secure Authentication**  
  Firebase Authentication with **Email & Password**, plus **OTP verification via EmailJS**.

- **Research History**  
  Persistent research history stored in Firestore with:
  - Local searching & filtering  
  - Ability to delete individual records

- **PDF Export**  
  Generate and download professional **PDF research reports**.

- **Privacy-Centric Design**  
  Users can permanently delete their account and all associated data with a single action.

---

## 🚀 Technical Stack

- **Frontend:** Flutter (Dart)  
- **Backend:** Firebase (Authentication, Firestore)  
- **AI Model:** Google Gemini API  
- **Email Service:** EmailJS (REST API)  
- **State Management:** StatefulWidgets with asynchronous Firestore listeners  

---

## 🛠️ Setup & Installation

### Prerequisites
- Flutter SDK installed
- A Firebase Project
- A Google AI Studio API Key
- An EmailJS account

### Configuration

1. **Clone the repository:**
   ```bash
    git clone [https://github.com/yourusername/echolens.git](https://github.com/yourusername/echolens.git)
    cd echolens
   ```
2. **Environment Variables:** Create a .env file in the root directory and add your credentials:
   ```.env
    GEMINI_API_KEY=your_gemini_key_here
    EMAILJS_SERVICE_ID=your_service_id
    EMAILJS_TEMPLATE_ID=your_template_id
    EMAILJS_PUBLIC_KEY=your_public_key
    ADMIN_EMAIL=admin@example.com
   ```
3. **Firebase Setup:**
   - Add your Android/iOS app to the Firebase console.
   - Download and place google-services.json (Android) or GoogleService-Info.plist (iOS) in the appropriate directories, or      use flutterfire configure.
4. **Install Dependencies:**
   ```bash
   flutter pub get
   ```
5. **Run the app:**
   ```bash
   flutter run
   ```
