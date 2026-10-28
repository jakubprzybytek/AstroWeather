import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { Amplify } from 'aws-amplify'
import { Authenticator } from '@aws-amplify/ui-react'
import { Button } from 'react-bootstrap'
import '@aws-amplify/ui-react/styles.css'
import App from './App.tsx'
import './styles/main.scss'

// The API accepts only users in the stage's groups (docs/development.md), so
// there is no sign-up here; the Authenticator handles sign-in and password reset.
Amplify.configure({
  Auth: {
    Cognito: {
      userPoolId: import.meta.env.VITE_USER_POOL_ID,
      userPoolClientId: import.meta.env.VITE_USER_POOL_CLIENT_ID,
      loginWith: { email: true }
    }
  }
})

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <Authenticator loginMechanisms={['email']} hideSignUp>
      {({ signOut, user }) => (
        <>
          <div className="auth-bar">
            <span>{user?.signInDetails?.loginId}</span>
            <Button variant="outline-secondary" size="sm" onClick={signOut}>Sign out</Button>
          </div>
          <App />
          <footer className="app-version" data-testid="app-version">AstroWeather {import.meta.env.VITE_APP_VERSION ?? 'dev'}</footer>
        </>
      )}
    </Authenticator>
  </StrictMode>,
)
