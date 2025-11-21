import { useCallback, useEffect } from 'preact/hooks';
import { useAuthStore } from '@/store/auth';
import { useRouter } from '../hooks/use-router';

interface AuthGuardProps {
    children: React.ReactNode;
}
const enableAuth = import.meta.env.VITE_ENABLE_AUTH === 'true';

export default function AuthGuard({ children }: AuthGuardProps) {
    const { isValidateToken, loading, initValidateToken } = useAuthStore();
    const router = useRouter();
    
    const checkAuth = useCallback(() => {
        // debugger;
        initValidateToken();
        if (!isValidateToken && !loading && enableAuth) {
            router.replace('/login');
        }
    }, [isValidateToken, loading, router]);

    useEffect(() => {
        // Initialize authentication state when AuthGuard mounts
        initValidateToken();
    }, [initValidateToken]);

    useEffect(() => {
        checkAuth();
    }, [checkAuth]);

    if (!isValidateToken && enableAuth) {
        return null;
    }

    return children;
}